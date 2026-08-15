# Cuelet for macOS

This directory contains the native SwiftUI/AppKit macOS client. It is a Swift Package Manager executable with a script that wraps the release product in a Finder-launchable app bundle; there is currently no Xcode project or workspace in the repository.

It also contains an independently buildable standard-C Audio Server Driver Plug-in under `Driver/`. The driver is not a SwiftPM target and does not require an Xcode project for the app.

## Requirements and build model

- Swift tools version: 5.10
- Release icon toolchain: Xcode 26 or later (for the checked-in Icon Composer asset)
- Deployment target: macOS 14 or later
- Native frameworks: SwiftUI, AppKit, AVFoundation, Core Audio, Carbon, and Service Management
- Current package product: arm64 on an Apple Silicon host (SwiftPM builds the host architecture)
- App bundle identifier: `ch.oki.cuelet`
- Local bundle signing: ad-hoc with Hardened Runtime enabled
- Sandbox: disabled
- Hardened Runtime: enabled for bundled Release builds
- Installer package: configured for local structural validation and future Developer ID signing
- Production signing, notarization, and updater: credentials/pipeline not configured

Open the package in Xcode when an IDE is useful:

```bash
open apps/macos/Package.swift
```

Do not use an invented Xcode scheme in automation. Add an `xcodebuild` command only after a project or workspace with a checked-in scheme exists.

## Build, test, and run

```bash
cd apps/macos
swift build
swift test
swift build -c release
swift test -c release
swift run Cuelet
```

The SwiftPM executable is useful for development, but app identity, restoration, privacy prompts, and background shortcut behavior should be validated with the real bundle:

```bash
cd apps/macos
./scripts/build-macos.sh
open dist/macos/Cuelet.app
```

The script creates `dist/macos/Cuelet.app`, writes the bundle `Info.plist`, compiles `Cuelet/Resources/Cuelet.icon` into the modern `Assets.car` plus a `Cuelet.icns` compatibility resource, builds and embeds the diagnostics-disabled Release HAL, and ad-hoc signs the result with Hardened Runtime when `codesign` is available. The Icon Composer source is not shipped in the app. Scratch and output locations can be isolated:

```bash
CUELET_DIST_DIR="$TMPDIR/cuelet-dist/macos" \
CUELET_SWIFTPM_SCRATCH_PATH="$TMPDIR/cuelet-swift-build" \
./scripts/build-macos.sh
```

Set `CUELET_VIRTUAL_AUDIO_DRIVER_BUNDLE` only when intentionally packaging a separately verified bundle. The app build never installs or activates a driver.

## End-user Installer package

Build the standard two-component macOS Installer product locally with:

```bash
cd apps/macos
./scripts/build-release-package.sh --local
```

This creates `dist/macos/Cuelet-0.1.0-local.pkg`. It installs the application
at `/Applications/Cuelet.app` and the audio driver at
`/Library/Audio/Plug-Ins/HAL/CueletVirtualAudio.driver`. The builder consumes
the normal Release app and production diagnostics-disabled HAL artifacts,
checks their versions, architecture, signatures, hashes, and contents, then
expands and tests the result without installing it. Its welcome screen derives
its Cuelet branding from the product name and polished Installer copy, while
the payload retains the final compiled app icon. Local packages are marked
“LOCAL TEST PACKAGE — NOT FOR PUBLIC DISTRIBUTION” on the Welcome, Read Me,
and Summary pages. The separately rendered public pages contain no local-test
copy.

The package supports clean install, same-version repair, and upgrades from an
older Cuelet app or driver. It refuses to replace a foreign bundle at either
exact destination and refuses to downgrade a newer Cuelet bundle. The
installation does not change audio defaults or restart Core Audio; Installer
finishes with the standard Close action. Cuelet is usable immediately, while
Cuelet Virtual Microphone remains unavailable until the user next restarts the
Mac. Cuelet reports that state as Restart required rather than Ready.

The full product installs both components system-wide and therefore enables
only Installer's local-system domain. It requires administrator authorization,
does not enable “Install for me only,” and does not expose component
customization. The app itself has no fixed `/Applications` dependency: for
app-only local use, `Cuelet.app` may instead be copied to `~/Applications` (or
another user-owned Applications folder). Normal library/playback features work,
but Cuelet Virtual Microphone is not installed. A separate app-only package is
not produced for 0.1.0; a drag-and-drop container is cleaner than a second
Installer receipt when that distribution channel is added.

Public release mode requires externally configured Developer ID Application
and Developer ID Installer identity names and never falls back to ad-hoc or
unsigned output:

```bash
CUELET_DEVELOPER_ID_APPLICATION='Developer ID Application: …' \
CUELET_DEVELOPER_ID_INSTALLER='Developer ID Installer: …' \
./scripts/build-release-package.sh --release
```

No credentials are stored in the repository. Notarization and stapling remain
separate explicit release operations. See [Installer/README.md](Installer/README.md)
for identifiers, signing order, upgrade policy, and uninstall/repair policy.

Useful local validation commands are:

```bash
codesign --verify --deep --strict --verbose=2 dist/macos/Cuelet.app
codesign -d --entitlements :- dist/macos/Cuelet.app
spctl --assess --type execute --verbose=4 dist/macos/Cuelet.app
```

An ad-hoc development bundle can pass structural `codesign` verification while Gatekeeper rejects it because it has no Developer ID signature or notarization ticket. That rejection is expected and is not a distribution pass.

## Isolated development validation

Cuelet Debug builds support two process-environment overrides for tests and local validation. They are compiled out of Release:

```bash
CUELET_APP_SUPPORT_DIR="$TMPDIR/cuelet-app-support" \
CUELET_LIBRARY_PATH="$TMPDIR/cuelet-library" \
dist/macos/Cuelet.app/Contents/MacOS/Cuelet
```

`CUELET_APP_SUPPORT_DIR` redirects settings and `CUELET_LIBRARY_PATH` selects an isolated library for that process. Use disposable media and directories; do not point destructive validation at a real sound library.

## Library and persistence behavior

Cuelet stores sound metadata in `.cuelet-metadata.json` at the selected library root using schema version 2. Each stored sound has a stable UUID and, where applicable, managed or linked storage, a managed relative path or external reference, a security-scoped bookmark, display/original names, missing state, favorite/category data, notes, aliases, shortcut metadata, cached duration, timestamps, and native filesystem identity.

- **Copy into Cuelet Library** creates a collision-safe file under `Sounds/`, never silently overwrites, and persists metadata before reporting success. A metadata failure rolls the new copy back.
- **Link External Files** preserves the source and stores a security-scoped bookmark where macOS permits it. Bookmark moves are followed when resolvable; stale or unavailable sources remain visible as missing.
- **Remove from Library** removes metadata only. For managed files, a tombstone prevents the next scan from silently re-importing the preserved file.
- **Delete Managed File** is shown only for an existing managed file. Cuelet revalidates containment, symlink/alias status, and recorded filesystem identity, stages the file in the same directory, commits metadata, then removes the staged file. Linked files cannot use this action.
- **Rename** changes only Cuelet's display name. It does not rename an external linked file.
- **Locate/Relink** restores a missing managed or linked entry while preserving its UUID and user metadata.

Metadata and settings writes use same-filesystem temporary files, POSIX rename, mode `0600`, and recovery copies. Version-1 metadata is backed up to `.v1.bak` before replacement. An unreadable entry or corrupt document produces a visible failure or recovery message instead of silently resetting the library.

The legacy settings-based favorites, categories, display names, and shortcuts are migrated when a library first adopts schema v2. The migration creates a settings safety copy, generates stable IDs when necessary, and is idempotent.

## Playback and shortcuts

Playback uses an injectable backend with one `AVAudioPlayer` per sound. The native adapter sets `AVAudioPlayer.currentDevice` to a stable Core Audio UID before preparing playback; `nil` means System Output. This preserves play, pause, resume, stop, stop all, simultaneous sounds, rapid replay, progress, end-of-file cleanup, linked-file security-scope lifetime, and termination cleanup without a broad engine rewrite. Missing, damaged, and route-rejected files do not enter playing state.

The global Cuelet volume is applied per player. When sounds overlap, each player receives `globalVolume / activePlayerCount` as conservative static headroom; Cuelet does not change system volume, normalize content, or apply compression. Route changes update active and paused players transactionally and roll back if any player rejects the destination.

Per-sound shortcuts use stable sound UUIDs. Global shortcuts use Carbon `RegisterEventHotKey`, work while Cuelet is in the background without Accessibility permission, restore after restart, and unregister on shutdown. The recorder temporarily suspends active global registrations so it can observe a key already assigned in Cuelet and show the conflict instead of triggering playback. Failed or conflicting assignments do not replace a working registration.

Local keyboard behavior remains separate:

- `Cmd+,` opens Settings.
- `Cmd+F` or `Ctrl+F` focuses search.
- Arrow keys move selection while the main library window is key and text input is not focused.
- Space or Return plays the selected sound.
- Escape clears search, then selection, then playback on successive presses.

## Audio devices and microphone scope

Settings enumerate alive Core Audio output devices with usable output streams and stable UIDs. Input-only, dead, UID-less, and duplicate entries are excluded. Temporary `AudioDeviceID` values are resolved only for the current session and are never persisted.

Supported output modes are:

- **System Output:** players use a `nil` device UID and follow the current macOS system output.
- **Explicit Output:** Cuelet resolves the saved UID to the current live device and routes every player to that UID without changing the system default.
- **Existing Virtual Output:** an installed virtual-transport output is selectable like any other explicit output. Product names are not hard-coded as the capability test.

The default device-loss policy is **Stop and Wait**. It stops affected playback, keeps the exact saved UID visibly unavailable, blocks new playback, and reconnects when that UID returns. **Temporarily Use System Output** is opt-in: it stops playback at the loss edge, leaves the original UID selected, and sends later playback to System Output until the exact device returns. A similarly named device is never substituted. Device-list, default-output, liveness, name, and output-stream changes are observed through Core Audio. Settings distinguishes selected, route-ready, unavailable, fallback, failure, and backend-confirmed active states; an idle selection is not presented as active.

Cuelet contains its own `Cuelet Virtual Microphone`, implemented as an Audio Server Driver Plug-in. The app distinguishes a bundled driver from a live input/output device by stable UID and never calls a file-only state Ready. A driver bundle changed after the current boot is shown as Restart required even if Core Audio still exposes the previously loaded device.

When the device is live, selecting it as Cuelet's explicit output injects soundboard playback into the driver's bounded output-to-input loopback. Receiving applications select `Cuelet Virtual Microphone` as their input. There is no driver IPC, physical-microphone mixing, simultaneous speaker output, system-default change, or in-app privileged helper. Current microphone metering remains local only.

## Cuelet Virtual Microphone driver

Build, test, package, and verify without root:

```bash
cd apps/macos
./scripts/build-virtual-audio-driver.sh Debug
./scripts/build-virtual-audio-driver.sh Release
./scripts/test-virtual-audio-driver.sh
./scripts/package-virtual-audio-driver.sh Release
./scripts/verify-virtual-audio-driver.sh \
  Driver/build/Release/CueletVirtualAudio.driver
```

The verifier expects a diagnostics-disabled production bundle by default. For
an intentionally diagnostic development bundle, set
`CUELET_EXPECT_DRIVER_DIAGNOSTICS=1` for that verification invocation.

Debug driver builds include bounded event telemetry and developer inspector tools. Release driver builds disable that telemetry and build only the HAL bundle. To reproduce a validation build explicitly, set `CUELET_DRIVER_DIAGNOSTICS=1`; do not use that variant as the public app payload.

The app compatibility baseline is transport version 0.1.8 build 9. Diagnostics-only HAL identities 0.1.9 build 10, 0.1.10 build 11, and 0.1.11 build 12 are accepted because they preserve that transport contract. This allowlist intentionally keeps transport compatibility separate from diagnostic bundle identity.

The public installation path is the Cuelet Installer package and requires no Terminal, repository checkout, Xcode, or Homebrew. For driver developers only, the explicit manual install and uninstall commands remain available:

```bash
./scripts/install-virtual-audio-driver.sh \
  Driver/build/Release/CueletVirtualAudio.driver
./scripts/uninstall-virtual-audio-driver.sh
```

Both development scripts require explicit typed confirmation, administrator approval for the exact system path, and a manual restart. Neither changes SIP, kills `coreaudiod`, changes audio defaults, or touches another vendor's bundle. Local artifacts are arm64-only and ad-hoc signed; public distribution still requires appropriate signing, hardened-runtime review, notarization, and stapling.

See `../../docs/cross-platform-catch-up/MACOS_VIRTUAL_AUDIO_DRIVER.md` for the object graph, real-time ring policy, stable identifiers, platform mapping, privacy behavior, and post-reboot validation plan.

## Test commands

Run the normal suite with `swift test`. It covers metadata migration/recovery, import and filesystem safety, managed/linked/missing policies, persistence rollback, playback lifecycle, shortcut identity/conflicts, search, and accessibility/action-policy labels.

Focused routing tests use fake player and Core Audio providers. Live installed-device routing is separate and opt-in; it plays only a short silent fixture, checks UID resolution/current-device confirmation, and verifies that the system default is unchanged:

```bash
CUELET_RUN_LIVE_AUDIO_TESTS=1 swift test \
  --filter AudioRoutingLiveIntegrationTests
```

The generated 250/1,000-file scanner and search benchmark is opt-in:

```bash
CUELET_RUN_PERFORMANCE_TESTS=1 swift test \
  --filter LibraryPerformanceTests/testLargeLibraryScanReloadAndSearch
```

See `../../docs/cross-platform-catch-up/MACOS_VALIDATION.md` for the latest verified hardware, results, screenshot manifest location, and remaining limitations.
