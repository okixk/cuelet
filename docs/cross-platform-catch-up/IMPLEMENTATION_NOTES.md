# Implementation notes

## Required parity

Match portable sound identity, relative paths, metadata precedence, favorites, category membership, aliases, shortcut conflict behavior, search normalization/ranking, sort choices, recent timestamps, duration display, and settings defaults. Enter should play the focused or best result; Escape should clear search before selection; missing files remain represented rather than silently deleted.

## Presentation that may be native

WinUI NavigationView, Mica, ContentDialog, tray icon, and XAML grid/list controls should become SwiftUI/AppKit navigation/menu-bar views and GTK/libadwaita views. Preserve information hierarchy and keyboard semantics, not pixel geometry.

## Categories and icons

Categories have stable IDs, names, color, icon, and ordering. Category deletion must not corrupt clips; reassignment/uncategorized behavior must be explicit. Use each platform's symbol/icon system with a stable mapping table and a fallback icon.

## Playback and volume

Windows MediaPlayer supports one or many active players, progress, duration discovery, stop-all, recent timestamps, and live volume. macOS uses AVAudioPlayer and has simultaneous-player behavior. Linux now uses a GStreamer service with explicit Playing/Paused/Stopped state, cached duration fingerprints, progress, volume, output targeting, and deterministic teardown. Ordinary local playback remains separate from virtual microphone routing; Linux targets the virtual sink only after the user explicitly enables the temporary route.

## Import and drag/drop

Copy mode makes a library-owned file; link mode retains the original path. Drop validation accepts supported audio only and reports rejected input. Linux file/folder dialog and `GdkFileList` drop imports share an immutable planner/executor that prevents overwrite and traversal, rejects symbolic links, reports partial results, and can restore a missing managed entry while preserving user metadata. An external path embedded only in portable metadata is not trusted: Linux exposes it as unavailable until the user explicitly imports the exact file, recording approval in private per-user settings. File drag-out remains pending. Category assignment changes metadata, not file ownership.

## Shortcuts and settings

Windows per-sound shortcuts are global and conflict-checked. macOS currently has a local keyboard service and capture validation. Linux captures and conflict-checks local shortcuts, and exposes stable commands for the GNOME custom-shortcut settings surface. It does not use X11-only grabs on Wayland. A future portal implementation must use the xdg-desktop-portal consent/session model and restore tokens. Linux XDG settings are validated separately from portable library metadata.

## Virtual audio

Windows has a platform-specific SysVAD-derived paired render/capture endpoint and WASAPI diagnostics. The normal graph is: Windows default output → physical speakers/headphones; Cuelet voice-chat output → Cuelet Virtual Microphone Input; chat microphone → Cuelet Virtual Microphone; Cuelet local playback → physical output. macOS should integrate an existing virtual device where appropriate.

Linux uses a temporary user-session `pw-loopback` graph: a Cuelet virtual
sink feeds a Cuelet virtual source, and Cuelet can target the sink through
GStreamer's PipeWire output. The graph uses non-lingering virtual nodes,
disables autoconnect, keeps exact child ownership, and changes neither default
device nor persistent configuration. Current Linux behavior does not
simultaneously monitor the virtual route on physical speakers and does not mix
a physical microphone. Compatibility was proven with PipeWire tools and a
generated WAV, not Discord or OBS.

## Lifecycle

Shutdown must stop playback, unregister shortcuts, release audio resources, and preserve settings. Windows supports tray/background and single-instance forwarding. macOS menu-bar and Linux desktop lifecycle are native adaptations. Linux forwards command lines to one `GApplication`, supports show/hide/exit, withdraws playback notifications, stops GStreamer players, and terminates only its owned PipeWire child. A stock-GNOME tray is intentionally not claimed.
