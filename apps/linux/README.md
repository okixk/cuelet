# Cuelet for Linux

This is the native Linux frontend for Cuelet. It is GTK4/libadwaita-based and targets Ubuntu/GNOME/Wayland. The older Qt prototype remains at the repository root as a reference, not the final Linux UI.

## Ubuntu Dependencies

```bash
sudo apt install build-essential meson ninja-build pkg-config \
  libgtk-4-dev libadwaita-1-dev \
  libgstreamer1.0-dev libgstreamer-plugins-base1.0-dev \
  libjson-glib-dev
```

Runtime codec support depends on installed GStreamer plugins. For common desktop audio formats on Ubuntu, install the usual good/bad/ugly/libav plugin packages as needed.

## Build and Run

```bash
cd /home/oki/projects/cuelet/apps/linux
meson setup build --wipe
meson compile -C build
./build/cuelet
```

Optional demo mode:

```bash
./build/cuelet --demo
```

## Command Line and GNOME Shortcuts

Cuelet forwards commands to the running application instance. Useful commands include:

```bash
./build/cuelet --list-sounds
./build/cuelet --list-sounds --json
./build/cuelet --list-categories --json
./build/cuelet --play-id SOUND_ID
./build/cuelet --play-name "Sound Name"
./build/cuelet --play-file /path/to/sound.wav
./build/cuelet --stop SOUND_ID
./build/cuelet --stop-all
./build/cuelet --show
./build/cuelet --hide
./build/cuelet --rescan
./build/cuelet --library /path/to/library
```

The `command:` field from `--list-sounds` is ready to paste into a GNOME custom
keyboard shortcut. It uses the absolute Cuelet executable path and stable sound ID.

Run tests:

```bash
meson test -C build --print-errorlogs
```

## Implemented

- GTK4/libadwaita application window with GNOME headerbar and navigation split view.
- Sidebar for Library, Favorites, Recent, All Categories, Uncategorized, and custom categories.
- Real library folder selection, recursive scanning, rescanning, and import/copy into the library.
- Supported scan extensions: `mp3`, `wav`, `ogg`, `flac`, `m4a`, `aif`, `aiff`.
- In-library `.cuelet-metadata.json` persistence for display names, categories, colors, icons, favorites, shortcuts, notes, aliases, added dates, and recent plays.
- Grid and list modes with a working toggle.
- Search, sort menu, Enter-to-play top search result, and Escape-to-clear-or-stop.
- Native popover context menus for sounds, categories, and empty library state.
- Category create, rename, delete, color change, icon change, and assignment.
- Favorite toggles in grid/list/context menu.
- Local per-sound shortcut recorder with conflict detection.
- GStreamer playback, single-sound stop, Stop All, simultaneous playback setting, volume setting, playing-state cleanup, and mini now-playing bar.
- GNOME-style preferences dialog sections for Library, Playback, Audio, Shortcuts, Appearance, Import Behavior, and Advanced.

## Current Limitations

- Global hotkeys are intentionally not implemented on Wayland. Local app shortcuts work while Cuelet is focused. Future options are xdg-desktop-portal support, compositor-specific APIs, and explicit X11 fallback support.
- Output device selection is documented in preferences but not wired yet; playback uses GStreamer's default output.
- Duration metadata is discovered through GStreamer where possible. Files/codecs without discoverer support fall back to `--:--`.
- Remove from Library removes a sound from the current view and metadata. A later rescan will show the file again if it is still inside the selected library folder.
- The macOS SwiftUI app is unchanged. It still stores its app settings in Application Support; the Linux app writes the shared in-library metadata schema documented in `docs/metadata-schema.md` for future macOS adoption.
