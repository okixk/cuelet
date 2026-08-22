# Linux Design

Cuelet's Linux app should feel like a GNOME utility, not a macOS clone and not a web app.

## Toolkit

- GTK4 for widgets and input.
- libadwaita for application/window structure, headerbar, navigation split view, status pages, toasts, and preferences.
- GStreamer for Linux-native playback and duration discovery.
- JSON-GLib for the Linux metadata/settings adapter.

## Main Window

The main surface uses:

- `AdwApplication`
- `AdwApplicationWindow`
- `AdwHeaderBar`
- `AdwNavigationSplitView`
- `GtkListBox` sidebar
- `GtkFlowBox` grid
- `GtkListBox` list mode
- `GtkPopover` context menus
- `GtkFileDialog`
- `AdwPreferencesDialog`

The headerbar owns library/import actions, sorting, grid/list mode, preferences, and Stop All. The sidebar owns navigation and category context menus. The content area owns title, library path, search, count, sound grid/list, empty states, and the now-playing bar.

## Platform Behavior

Desktop-wide shortcuts use the standards-based XDG GlobalShortcuts portal and
show the portal's accepted trigger description and approval state. Local GTK
shortcuts remain available while Cuelet is focused, and a generated
`cuelet --play-id` command can be assigned through GNOME Custom Shortcuts when
the portal is unavailable or a request is denied.

## Visual Direction

The GTK app uses native spacing, headerbar controls, popovers, status pages, and preferences rows. Category color is shown as small chips/dots. Sound cards are compact and card-like but stay within GNOME visual conventions.

The UI should not copy macOS titlebar layout, SwiftUI cards, or SF Symbols naming. Feature parity matters more than visual parity.
