# macOS Design Direction

Cuelet's macOS app should feel like a real macOS utility, not a themed cross-platform prototype.

## UI Principles

- Use system font, native spacing, and platform controls.
- Prefer `NavigationSplitView`, `Form`, `Picker`, `Menu`, `Button`, `Toggle`, and SwiftUI toolbar items before custom controls.
- Keep blue accent usage focused on selection, active playback, and primary actions.
- Follow system light and dark mode.
- Keep technical paths and diagnostics out of primary settings screens.

## Main Window

The primary window uses a native sidebar with three initial destinations: Sound Library, Overlay Preview, and Settings. The Sound Library page is the main workflow and should receive the most design attention.

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

## Overlay

The overlay is only a preview for now. Future work can add an always-on-top floating pad grid, profile selector, opacity setting, show/hide shortcut, and persistent Stop All button.
