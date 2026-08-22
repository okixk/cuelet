# macOS Design Direction

Cuelet's macOS app should feel like a real macOS utility, not a themed cross-platform prototype.

## UI Principles

- Use system font, native spacing, and platform controls.
- Prefer `NavigationSplitView`, `Form`, `Picker`, `Menu`, `Button`, `Toggle`, and SwiftUI toolbar items before custom controls.
- Keep blue accent usage focused on selection, active playback, and primary actions.
- Follow system light and dark mode.
- Keep technical paths and diagnostics out of primary settings screens.

## Main Window

The primary window uses a native sidebar for Library, Favorites, Recent, and
Categories. The Sound Library page is the main workflow. Settings is exposed
through the macOS Settings scene, not as a sidebar destination.

The toolbar groups frequent actions by intent:

- Library: Choose Library, Import Sounds, Rescan.
- Playback: Play Selected, Stop All.
- View: Grid/List toggle and overlay toggle.

Less common actions belong in context menus, app menus, or overflow menus.

## Sound Pads

Pads should be visual and scannable. Avoid dense metadata rows. The target layout is:

- Top: waveform or icon preview with play/playing indicator.
- Middle: sound name, ellipsized when needed.
- Bottom left: category chip.
- Bottom right: shortcut badge or duration.

There is no overlay destination in the current main window.
