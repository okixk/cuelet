# Implementation notes

## Required parity

Match portable sound identity, relative paths, metadata precedence, favorites, category membership, aliases, shortcut conflict behavior, search normalization/ranking, sort choices, recent timestamps, duration display, and settings defaults. Enter should play the focused or best result; Escape should clear search before selection; missing files remain represented rather than silently deleted.

## Presentation that may be native

WinUI NavigationView, Mica, ContentDialog, tray icon, and XAML grid/list controls should become SwiftUI/AppKit navigation/menu-bar views and GTK/libadwaita views. Preserve information hierarchy and keyboard semantics, not pixel geometry.

## Categories and icons

Categories have stable IDs, names, color, icon, and ordering. Category deletion must not corrupt clips; reassignment/uncategorized behavior must be explicit. Use each platform's symbol/icon system with a stable mapping table and a fallback icon.

## Playback and volume

Windows MediaPlayer supports one or many active players, progress, duration discovery, stop-all, recent timestamps, and live volume. macOS uses AVAudioPlayer and has simultaneous-player behavior; Linux must define an equivalent service. Local playback is separate from virtual microphone routing. Do not route local sound to a virtual endpoint merely because it exists.

## Import and drag/drop

Copy mode makes a library-owned file; link mode retains the original path. Drop validation accepts supported audio only, reports rejected input, and permits file drag-out without deleting source data. Category drops change metadata, not file ownership.

## Shortcuts and settings

Windows per-sound shortcuts are global and conflict-checked. macOS currently has a local keyboard service and capture validation; Linux needs a desktop-appropriate strategy. Persist settings independently from portable library metadata. Routing diagnostics must expose endpoint names, direction, connection state, and warnings.

## Virtual audio

Windows has a platform-specific SysVAD-derived paired render/capture endpoint and WASAPI diagnostics. The normal graph is: Windows default output → physical speakers/headphones; Cuelet voice-chat output → Cuelet Virtual Microphone Input; chat microphone → Cuelet Virtual Microphone; Cuelet local playback → physical output. macOS should integrate an existing virtual device where appropriate; Linux should use PipeWire/PulseAudio nodes. Do not promise a new kernel/native driver in this documentation task.

## Lifecycle

Shutdown must stop playback, unregister shortcuts, release audio resources, and preserve settings. Windows supports tray/background and single-instance forwarding. macOS menu-bar and Linux desktop lifecycle are native adaptations.

