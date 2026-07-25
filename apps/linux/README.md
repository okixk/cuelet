# Cuelet for Linux

This is the native Linux frontend for Cuelet. It is GTK4/libadwaita-based and targets Ubuntu/GNOME/Wayland. The older Qt prototype remains at the repository root as a reference, not the final Linux UI.

## Ubuntu Dependencies

```bash
sudo apt install build-essential meson ninja-build pkg-config \
  libgtk-4-dev libadwaita-1-dev \
  libgstreamer1.0-dev libgstreamer-plugins-base1.0-dev \
  libjson-glib-dev pipewire-bin
```

Runtime codec support depends on installed GStreamer plugins. For common desktop audio formats on Ubuntu, install the usual good/bad/ugly/libav plugin packages as needed.
`pipewire-bin` supplies `pw-loopback` for the optional temporary virtual
microphone route; the rest of Cuelet works without enabling that route.

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
./build/cuelet --exit
./build/cuelet --rescan
./build/cuelet --library /path/to/library
```

The `command:` field from `--list-sounds` is ready to paste into a GNOME custom
keyboard shortcut. It uses the absolute Cuelet executable path and stable sound ID.

Run tests:

```bash
meson test -C build --print-errorlogs
```

Warnings-as-errors Debug and optimized Release builds:

```bash
meson setup build-debug --wipe -Dbuildtype=debug -Dwerror=true
meson compile -C build-debug
meson test -C build-debug --print-errorlogs

meson setup build-release --wipe -Dbuildtype=release -Dwerror=true
meson compile -C build-release
meson test -C build-release --print-errorlogs
```

## Implemented

- GTK4/libadwaita application window with GNOME headerbar and navigation split view.
- Sidebar for Library, Favorites, Recent, All Categories, Uncategorized, and custom categories.
- Real library folder selection, recursive scanning, rescanning, file/folder dialog import, and `GdkFileList` drag-and-drop import.
- Copy and link import modes with collision handling, duplicate detection, missing-file recovery, provenance, partial-result reporting, symbolic-link rejection, and no overwrite of an existing destination.
- Supported scan extensions: `mp3`, `wav`, `ogg`, `flac`, `m4a`, `aif`, `aiff`.
- In-library `.cuelet-metadata.json` persistence for display names, categories, colors, icons, favorites, shortcuts, notes, aliases, added dates, recent plays, linked sources, and duration fingerprints.
- Atomic metadata/settings writes and tolerant loading of malformed, missing, and legacy values. Linux settings are private to the user (`0600`).
- Grid and list modes with a working toggle.
- Search, sort menu, Enter-to-play top search result, and Escape-to-clear-or-stop.
- Native popover context menus for sounds, categories, and empty library state.
- Category create, rename, delete, color change, icon change, and assignment; managed rename and linked display-name editing are kept distinct.
- Favorite toggles in grid/list/context menu.
- Local per-sound shortcut recorder with conflict detection.
- GStreamer playback with pause/resume, progress, cached duration, single-sound stop, Stop All, simultaneous playback, volume, explicit PipeWire/PulseAudio target selection, cleanup, and a native mini-player.
- Single-instance command forwarding, show/hide/exit lifecycle, clean playback shutdown, and a GNOME notification when hidden playback starts.
- A temporary, user-session PipeWire virtual microphone route with scoped child-process ownership, diagnostics, rollback, exact-handle cleanup, and no default-device or persistent-configuration changes.
- GNOME-style preferences dialog sections for Library, Playback, Audio, Shortcuts, Appearance, Import Behavior, and Advanced.
- Keyboard navigation and screen-reader labels/selection state on sound cards and rows.

## Current Limitations

- GNOME Wayland does not expose unrestricted global key grabs. Local shortcuts work while Cuelet is focused; the sound menu supplies a stable command for GNOME Settings → Keyboard → Custom Shortcuts. An xdg-desktop-portal GlobalShortcuts consent/session implementation remains future work.
- Stock GNOME has no reliable built-in StatusNotifierItem tray surface. Cuelet supports single-instance show/hide/exit and notifications, but it exits when its window is closed and does not promise tray/background behavior.
- The temporary virtual microphone sends Cuelet playback to the virtual source instead of local speakers. Simultaneous local monitoring and microphone mixing need an explicit PipeWire/GStreamer mixer design with loop prevention.
- The route was validated with PipeWire tools and a generated WAV. Discord, OBS, and other receiving applications were not installed or tested, so compatibility is not claimed.
- Explicit output selection accepts a current PipeWire `target-object` or PulseAudio device identifier; Cuelet does not yet enumerate friendly device names.
- Link imports approve the exact external file in per-user settings. External paths found only in portable library metadata remain unavailable until the user explicitly imports them; this prevents a copied metadata file from causing automatic reads outside the library.
- Duration metadata is discovered through GStreamer where possible. Files/codecs without discoverer support fall back to `--:--`.
- Remove from Library removes a sound from the current view and metadata. A later rescan will show the file again if it is still inside the selected library folder.
- Import drag-in is implemented; file drag-out is not.
- The macOS SwiftUI app is unchanged. It still stores its app settings in Application Support; the Linux app writes the shared in-library metadata schema documented in `docs/metadata-schema.md` for future macOS adoption.
