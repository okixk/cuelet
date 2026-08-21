# Cuelet for Windows

Cuelet for Windows is a native WinUI 3 and C++/WinRT application. Debug builds run unpackaged for a quick edit/build/run loop; Release builds use a single-project packaged MSIX configuration.

## Implemented phases

1. **Foundation** — Windows App SDK 2.2, C++/WinRT, XAML, Mica, integrated title bar, `NavigationView`, build/run/package scripts, and the shared-core MSVC static library.
2. **Library** — startup library onboarding, persisted folder selection, recursive scanning through `cuelet-core`, search, favorites/recent/category scopes, sorting, grid/list views, copy/link import, interoperable file drag-and-drop, rescan, missing-file handling, and useful empty/error states.
3. **Playback** — native `MediaPlayer` playback, overlapping or single-sound mode, live volume changes, duration discovery, playback progress, recent timestamps, and Stop All.
4. **Organization** — portable `.cuelet-metadata.json` schema-v2 read/write, conservative v1 backup, editable names/notes/aliases/categories/favorites/shortcuts, and category create/rename/delete workflows.
5. **Windows integration** — global per-sound shortcuts, Enter-to-play search, Escape/Ctrl+F commands, tray/background operation, single-instance CLI forwarding, Mica, native pickers, hidden metadata attributes, and registry-backed user settings that work in packaged and unpackaged builds.
6. **Hardening** — Debug and Release configurations, Windows core regression tests, unsigned development packaging, runtime smoke coverage, settings/about UI, and updated documentation.
7. **Virtual microphone** — Release builds detect and route to a separately installed VB-CABLE pair; Debug builds retain the pinned SysVAD test package, constrained elevated helper, and independent WASAPI render-to-capture tooling for driver development.

`Cuelet.Core.Win32` compiles toolkit-neutral sound types, stable IDs, scanning, search, filtering, and sorting from `core/cuelet-core`. Windows JSON persistence remains a Windows adapter because the shared `MetadataStore.cpp` currently depends on JSON-GLib.

## Requirements

- Visual Studio Community 2026 18.7 or newer
- MSVC v145 x64 tools
- Windows application development C++ components
- Windows 11 SDK 10.0.26100 or newer
- Developer Mode for local package deployment
- VB-CABLE from VB-Audio, followed by a Windows restart, when voice-chat
  microphone routing is required; it is a third-party donationware driver and
  is not bundled with Cuelet

The project pins the stable `Microsoft.WindowsAppSDK` NuGet package at `2.2.0`.

## VB-CABLE routing

The Windows Release app recognizes VB-Audio's matching `CABLE Input` playback
endpoint and `CABLE Output` recording endpoint. Install VB-CABLE from
`https://vb-audio.com/Cable/` using VB-Audio's administrator/restart process,
then choose **Refresh audio devices** or run Audio Setup again. Cuelet sends its
soundboard mix to `CABLE Input`; select `CABLE Output` as the microphone in
Discord, games, OBS, or another receiving application. Cuelet can mix the
selected physical microphone into that route and can monitor locally through a
separate speaker/headphone device.

Cuelet links to the vendor download page but does not download, redistribute,
install, repair, or uninstall VB-CABLE. End users remain responsible for the
vendor's license and installation terms.

## Build and run

From the repository root:

```powershell
powershell -ExecutionPolicy Bypass -File .\apps\windows\scripts\build-windows.ps1 -Configuration Debug
powershell -ExecutionPolicy Bypass -File .\apps\windows\scripts\run-windows.ps1 -NoBuild
```

Debug is intentionally unpackaged, so the generated executable can run directly. Settings live under `HKCU\Software\Cuelet`; library metadata lives in the selected folder.

## Command line

Commands are forwarded to an already running Cuelet instance, so they do not create a second window. Text output is UTF-8; `--json` writes only the requested JSON document to standard output.

```text
Cuelet.exe --help
Cuelet.exe --list-sounds [--json]
Cuelet.exe --list-categories [--json]
Cuelet.exe --play-id <sound-id>
Cuelet.exe --play-name <name>
Cuelet.exe --play-file <path>
Cuelet.exe --stop <sound-id>
Cuelet.exe --stop-all
Cuelet.exe --show
Cuelet.exe --hide
Cuelet.exe --rescan
Cuelet.exe --library <folder>
Cuelet.exe --import <path> [--import <path> ...] [--mode copy|link] [--category <name>] [--json]
Cuelet.exe --reveal-id <sound-id>
Cuelet.exe --create-library <folder>
Cuelet.exe --use-library <folder>
```

`--library <folder>` can be combined with another command. Paths with spaces should be quoted. Exit code `0` indicates success, `2` invalid syntax, `3` missing library/sound/source, `4` a filesystem or metadata failure, and `5` a single-instance forwarding failure.

The WinUI executable uses the Windows GUI subsystem. PowerShell displays its output, but does not always wait before updating `$LASTEXITCODE`; scripts that consume the exit code should use `Start-Process -Wait -PassThru` (and redirect standard output/error when needed).

Release remains the packaged configuration:

```powershell
powershell -ExecutionPolicy Bypass -File .\apps\windows\scripts\build-windows.ps1 -Configuration Release
```

The root `VERSION` value `0.1.0` maps to the required four-component MSIX and
PE file version `0.1.0.0`; the final component is the Windows package revision.
The Release configuration is x64, targets Windows SDK 10.0.26100, and declares
Windows 10 version 1809 (`10.0.17763.0`) as its minimum.

## Tests

```powershell
powershell -ExecutionPolicy Bypass -File .\apps\windows\scripts\test-windows.ps1 -Configuration Debug
powershell -ExecutionPolicy Bypass -File .\apps\windows\scripts\test-windows.ps1 -Configuration Release
```

The Windows test target covers scanning, Unicode paths, stable IDs, search ranking, managed/link import planning, duplicate detection, transactional rename rollback, global shortcut migration, CLI parsing, hidden metadata attributes, startup-library state, notification timeout policy, endpoint ownership/pairing, installer workflow states, and shutdown coordination.

## Package

```powershell
powershell -ExecutionPolicy Bypass -File .\apps\windows\scripts\package-windows.ps1 -Configuration Release
```

This creates an unsigned local MSIX. Signing identity, certificate distribution, Store submission, and production installer policy are release-management work and are intentionally not faked by the script. Ordinary Release builds exclude the development virtual-audio driver and installer. Developers can opt into those artifacts explicitly with `-p:CueletIncludeDevelopmentVirtualAudioDriver=true` after preparing the test package.

## Public 0.x beta

The normal public Windows beta artifact is the unsigned portable ZIP, not the
unsigned MSIX. Build it with:

```powershell
powershell -ExecutionPolicy Bypass -File .\apps\windows\scripts\package-windows-beta.ps1
```

This produces `Cuelet-0.1.0-beta.1-windows-x64-unsigned.zip`. It contains the
self-contained x64 Release application layout, the full `LICENSE`, and the
beta notice/instructions. It does not contain an MSIX, Cuelet's development
SysVAD driver, or VB-CABLE. Windows 10 version 1809 or later is supported;
install VB-CABLE separately when virtual microphone routing is needed.
The executable is intentionally unsigned, so Windows or SmartScreen may warn
when it is launched.

The existing `package-windows.ps1` MSIX route remains available for local
package validation and future signed releases. It must not be presented as the
normal public beta installer while it is unsigned. Do not add a certificate or
change the production identity/signing boundary to prepare this beta.

Release packaging includes the repository's exact `LICENSE` at the MSIX package
root. The package command audits the generated archive and fails unless that file
is byte-identical to the root `LICENSE`; it also rejects development driver,
debug/source/test, signing-secret/certificate, and developer-path content.

The manifest keeps the existing local identity `ch.oki.cuelet` and the explicit
development publisher placeholder `CN=Cuelet Development`. These values must be
replaced together with the real Store identity or the subject of the actual
production signing certificate. `release-identity.psd1` is the checked source of
truth used by release validation; update it and `Package.appxmanifest` together.
The repository does not contain or generate a certificate. The unsigned artifact
is suitable for build and content validation, but normal installation on another
machine and SmartScreen reputation require a trusted signed package.

For Microsoft Store distribution, reserve Cuelet in Partner Center and copy the
assigned Package ID, Publisher ID, and publisher display name exactly; do not
guess them. Store submission packages do not require a CA-trusted signature
because Microsoft signs accepted packages. For direct MSIX distribution, obtain
a trusted code-signing certificate whose subject exactly matches the manifest
Publisher, import it into the current-user or local-machine Personal store, then
build and sign a copy of the unsigned package:

```powershell
powershell -ExecutionPolicy Bypass -File .\apps\windows\scripts\package-windows.ps1 -Configuration Release
powershell -ExecutionPolicy Bypass -File .\apps\windows\scripts\sign-windows-package.ps1 `
  -PackagePath <unsigned.msix> `
  -CertificateThumbprint <production-code-signing-thumbprint> `
  -OutputPath <signed.msix> `
  -TimestampUrl <production-rfc3161-timestamp-url>
```

The signing helper verifies the configured identity, certificate subject,
private key, validity period, and code-signing usage. It refuses the development
publisher unless `-AllowDevelopmentPublisher` is explicitly supplied for a
disposable local installation test, and it never overwrites the unsigned input.
PFX files, private keys, and exported certificates are ignored under
`apps/windows`; keep production credentials in an external secure store.

Before packaging, the script verifies that release metadata matches `VERSION`,
that only x64 project configurations are present, and that every prepared
Windows icon resource resolves. After packaging, it audits the actual MSIX
manifest, contents, and repository LICENSE equality. The final icon is supplied
as scale-aware MSIX PNGs plus a lossless multi-resolution executable icon used
by the window, task switcher, and notification area. The supplied placeholder
lock-screen and backup Store images are not packaged.

## Current boundaries

- Cuelet retains a pinned-SysVAD driver source overlay, render-to-capture ring,
  test-signed local Debug packaging, a constrained elevated installer helper,
  and a WASAPI flow verifier for engineering work. None of it is shipped in the
  Release MSIX. Publishing a Cuelet-owned driver later would require separate
  Microsoft-compatible production driver signing. See
  `docs/VIRTUAL_AUDIO_DRIVER.md`.
- Release packaging uses the shared Cuelet icon artwork across tile, Store,
  splash, and unplated icon sizes.
