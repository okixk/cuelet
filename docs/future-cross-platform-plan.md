# Future Cross-Platform Plan

Cuelet is moving away from one fake cross-platform UI. Each desktop platform should get a native frontend that uses the shared backend model where that makes sense.

## Native Frontends

- macOS: SwiftUI/AppKit under `apps/macos`.
- Windows: future native Windows frontend under `apps/windows`.
- Linux: future native Linux frontend under `apps/linux`.

The existing Qt prototype remains intact as a reference and checkpoint. It should not be deleted unless explicitly requested.

## Shared Core Candidates

`core/cuelet-core` can eventually own:

- Library scanning.
- Supported audio file detection.
- Sound metadata.
- Categories, favorites, and recent sounds.
- Profiles.
- Search and filtering.
- Import/export format.
- Settings model.
- Hotkey definitions.

Do not introduce FFI or a new systems-language backend until the native macOS app has validated the app model.
