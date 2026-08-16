# Shared Backend Plan

Cuelet has three native backend realities:

- The polished macOS SwiftUI app under `apps/macos`.
- The native Linux GTK/libadwaita app under `apps/linux`.
- The native Windows WinUI 3/C++/WinRT app under `apps/windows`.

The first Linux pass introduces `core/cuelet-core` as a small toolkit-neutral C++17 backend for shared concepts.

## Implemented in `core/cuelet-core`

- Sound/category/shortcut models.
- Library scanning and supported extension checks.
- Stable IDs for sounds and categories.
- Search/filter/sort behavior.
- In-library metadata load/save.

## Duplicated or Platform-Specific Today

- macOS still has Swift services for scanning, search, playback, settings, and shortcuts.
- Linux settings are stored in `~/.config/cuelet/settings.json`.
- Linux audio is implemented with GStreamer.
- macOS audio remains AVFoundation/AppKit/SwiftUI-specific.
- Windows compiles scanner/search/types into an MSVC static library, uses `MediaPlayer`, stores app preferences in `HKCU\Software\Cuelet`, and has a WinRT JSON schema-v2 adapter.

## Sharing Boundary

The immediate shared contract is the metadata schema and behavior, not a forced FFI bridge. The Linux app writes `.cuelet-metadata.json` inside the library folder. The macOS app can adopt that file later without changing its UI architecture.

## Future Steps

1. Teach macOS `SettingsStore`/`AppState` to import/export `.cuelet-metadata.json` while preserving existing Application Support settings.
2. Move category/search/sort behavior in Swift toward the documented core semantics.
3. Add a deliberate Swift bridge only if sharing compiled C++ becomes worth the build complexity.
