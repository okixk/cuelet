# Cross-Platform Plan

Cuelet uses a native frontend on each desktop platform and shares backend
models only where that preserves native behavior.

## Native Frontends

- macOS: SwiftUI/AppKit under `apps/macos`.
- Windows: WinUI 3/C++ under `apps/windows`.
- Linux: GTK4/libadwaita under `apps/linux`.

## Shared Core Candidates

`core/cuelet-core` owns or can grow to own:

- Library scanning.
- Supported audio file detection.
- Sound metadata.
- Categories, favorites, and recent sounds.
- Profiles.
- Search and filtering.
- Import/export format.
- Settings model.
- Hotkey definitions.

Introduce an FFI boundary only when sharing compiled core behavior is worth the
platform integration and maintenance cost.
