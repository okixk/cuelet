# Architecture

Cuelet has three native frontend tracks:

- The macOS SwiftUI frontend under `apps/macos`.
- The native Linux GTK/libadwaita frontend under `apps/linux`.
- The native Windows WinUI 3/C++ frontend under `apps/windows`.

## Layers

### UI Layer

Each UI layer is platform-native: SwiftUI/AppKit on macOS, GTK4/libadwaita on Linux, and WinUI 3/C++/WinRT on Windows. Shared behavior is kept below the view layer where doing so does not weaken native interaction patterns.

### App and Service Layer

The macOS app keeps services in Swift under `apps/macos/Cuelet/Services`:

- `LibraryService`
- `PlaybackService`
- `SettingsStore`
- `SearchService`
- `ProfileService`
- `HotkeyService`

Linux uses GStreamer and JSON-GLib adapters. Windows uses `MediaPlayer`, Windows pickers, a WinRT JSON metadata adapter, and registry-backed settings. Platform audio and lifecycle services stay native.

### Shared Core

`core/cuelet-core` owns toolkit-neutral sound/category/shortcut models, scanning, stable IDs, search, filtering, sorting, and the shared metadata contract. Linux consumes it directly; Windows compiles the toolkit-neutral portion as `Cuelet.Core.Win32`.

## Build Boundaries

Each native frontend is isolated under `apps/<platform>` and has its own build
system. Linux uses Meson, macOS uses Swift Package Manager and Xcode tooling,
and Windows uses Visual Studio/MSBuild. The shared `core/cuelet-core` sources
remain toolkit-neutral and are consumed only where the native build explicitly
includes them.
