# Cuelet for macOS

This folder contains the native macOS SwiftUI/AppKit frontend for Cuelet. It is intentionally separate from the existing Qt/CMake prototype at the repository root.

## Open in Xcode

Until the Xcode project is created through Xcode's project editor, open the Swift package directly:

```bash
open apps/macos/Package.swift
```

The intended permanent location for the native project is:

```text
apps/macos/Cuelet.xcodeproj
```

Create that project from Xcode and add the existing `Cuelet/` source tree to the app target. Avoid direct `project.pbxproj` edits while Xcode is open.

## Run from Terminal

For quick UI iteration, run the Swift package executable:

```bash
cd apps/macos
swift run Cuelet
```

To force the built-in demo library for development or screenshots:

```bash
cd apps/macos
swift run Cuelet -- --demo
```

This is not a full macOS app bundle. Use it for development only; app identity, restoration, privacy prompts, and some menu/shortcut registration behavior can differ from Finder or Xcode app launches.

## Build a Finder-launchable App

Build a real app wrapper with bundle metadata:

```bash
cd apps/macos
./scripts/build-macos.sh
open dist/macos/Cuelet.app
```

The script creates:

```text
apps/macos/dist/macos/Cuelet.app
```

In restricted build environments, override the output directory:

```bash
CUELET_DIST_DIR=/private/tmp/cuelet-dist/macos ./scripts/build-macos.sh
```

The generated app bundle includes:

- App name: `Cuelet`
- Bundle identifier: `ch.oki.cuelet`
- `NSMicrophoneUsageDescription`
- SwiftPM resource bundles copied into `Contents/Resources`
- Ad-hoc code signing when `codesign` is available

Double-clicking `dist/macos/Cuelet.app` from Finder is the preferred local test path for menu commands, app identity, Settings, and privacy prompts.

## Build from CLI

The current source tree can be validated with SwiftPM:

```bash
cd apps/macos
swift build
```

In this managed Codex session, SwiftPM needed writable scratch paths:

```bash
env CLANG_MODULE_CACHE_PATH=/private/tmp/cuelet-module-cache swift build --disable-sandbox --scratch-path /private/tmp/cuelet-swift-build
```

After `Cuelet.xcodeproj` is created through Xcode's project editor, the app target should also build with:

```bash
xcodebuild -project apps/macos/Cuelet.xcodeproj -scheme Cuelet -destination 'platform=macOS' build
```

## Keyboard Behavior

Cuelet uses native SwiftUI commands for menu shortcuts and an AppKit `NSEvent` local monitor for in-window soundboard keys:

- `Cmd+,` opens Settings.
- `Cmd+F` or `Ctrl+F` focuses search through app commands.
- Arrow keys move sound-pad selection when the main library window is key and text input is not focused.
- Space or Return plays the selected pad when the main library window is key and text input is not focused.
- Escape clears search, then sound selection, then playback on successive presses.
- Typing in search remains normal text input and does not trigger soundboard playback.
- Pressing Return while search is focused plays the selected visible result, or the top ranked visible result when no explicit result is selected.
- Escape while search is focused clears search first.

Per-sound shortcuts use structured key-code/modifier metadata with Local and Global scopes. Global shortcuts use Carbon `RegisterEventHotKey`, remain active while Cuelet runs in the background, and are updated transactionally so failed registrations do not overwrite working assignments.

## Library and Demo Behavior

`Choose Library...` opens a native folder picker and scans supported audio files from the chosen folder. Supported formats are `mp3`, `wav`, `m4a`, `aiff`, `aif`, and `flac` where macOS playback supports the file. The `Scan subfolders` setting controls recursive folder scanning and triggers a rescan when changed.

The selected folder path is persisted in Application Support and becomes the active library on the next launch. A real selected library takes priority over demo content. The demo library is optional: use the empty-state `Show Demo Library` button, the Settings toggle, or launch with `--demo` to force demo mode for development/testing.

Library sorting is persisted with the rest of Cuelet's settings. `Latest Added` and `Oldest Added` use each clip's `addedAt` timestamp; for scanned or imported files, Cuelet fills that from the file creation date when available, then the modification date, then deterministic scan/import order as a fallback.

## Runtime Notes

Running the Swift package executable is useful for UI iteration, but it is not the final app bundle shape. Bundle identity, app intents/shortcuts registration, window restoration, and microphone privacy prompts can emit warnings or behave differently until Cuelet has a signed macOS app target or app bundle with:

- A stable bundle identifier, currently `ch.oki.cuelet` in `scripts/build-macos.sh`.
- `NSMicrophoneUsageDescription` in `Info.plist`.
- The macOS audio input entitlement if sandboxing is enabled in a future Xcode app target.

The Audio & Microphone preferences intentionally avoid prompting for microphone access when the running bundle lacks the required usage description, because macOS can terminate apps that request capture access without the privacy key.

## Current Scope

- Native SwiftUI app entry point and settings scene with the standard macOS Settings menu item and `Cmd+,` shortcut.
- Native sidebar using `NavigationSplitView`, with Library, Favorites, Recent, category filters, and Overlay Preview.
- Native toolbar/menu controls.
- AppKit local keyboard handling for sound grid selection, playback, and stop commands without drawing a grid focus ring.
- Real folder scanning for common audio files, optional demo library mode, and persisted library/view/category settings.
- Search scoped to the selected sidebar filter, deterministic result ranking, selected state, recent tracking, and playing state.
- Stable sound pad grid with category chips, shortcut badges, waveform previews, hover, and Finder-style multi-selection.
- Finder-like list view with selectable rows and useful sound metadata.
- Settings view with Library, Playback, Audio & Microphone, Overlay, Appearance, Import Behavior, and Advanced areas.
- Portable category icon IDs mapped to SF Symbols, persisted colors, and a native category editor.
- Real shortcut recorder, conflict replacement, Carbon global shortcuts, menu-bar operation, and optional Launch at Login.
- Engine-derived mini-player progress and Finder reveal for single or multiple sounds.
- Audio routing foundation for device discovery, microphone permission state, local input metering, system-output playback, and honest virtual-device guidance.
- Finder-launchable app wrapper script at `scripts/build-macos.sh`.
- Placeholder overlay preview only.

Explicit non-default output routing, microphone mixing, virtual-device output, and floating overlay windows remain deferred. Cuelet does not create or install a virtual microphone.
