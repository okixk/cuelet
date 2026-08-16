# Cuelet for Linux

This is the native Linux frontend for Cuelet. It is GTK4/libadwaita-based and targets Ubuntu/GNOME/Wayland.

## Ubuntu Dependencies

The native client is validated on Ubuntu 26.04 LTS x86_64 with GTK 4.22.4,
libadwaita 1.9.1, GStreamer 1.28.2, JSON-GLib 1.10.8, Meson 1.10.1,
Ninja 1.13.2, and GCC 15.2.0. The Meson minimums remain GTK 4.10,
libadwaita 1.5, and GStreamer 1.20.

```bash
sudo apt install build-essential meson ninja-build pkg-config \
  libgtk-4-dev libadwaita-1-dev \
  libgstreamer1.0-dev libgstreamer-plugins-base1.0-dev \
  libjson-glib-dev desktop-file-utils appstream libxml2-utils \
  binutils file dbus-daemon tar gzip
```

Optional runtime codec and PipeWire support:

```bash
sudo apt install gstreamer1.0-plugins-base gstreamer1.0-plugins-good \
  gstreamer1.0-plugins-bad gstreamer1.0-plugins-ugly \
  gstreamer1.0-libav gstreamer1.0-pipewire pipewire-bin
```

Runtime codec support depends on the installed GStreamer plugins.
`pipewire-bin` supplies `pw-loopback` for the optional app-managed virtual
microphone and `gstreamer1.0-pipewire` supplies Cuelet's exact-target audio
stream. The rest of Cuelet works without enabling that feature.
PipeWire, PulseAudio, and xdg-desktop-portal are runtime integrations, not
compile-time dependencies. Desktop-wide shortcuts require the
`org.freedesktop.portal.GlobalShortcuts` interface; version 1 is supported and
was tested with xdg-desktop-portal 1.21.1 and the GNOME 50.0 backend. Automatic
playback uses the GStreamer sink selected by the desktop. The explicit
PipeWire/PulseAudio choices and temporary virtual route are enabled only when
their corresponding runtime tools/plugins exist.

## Build and Run

```bash
cd /path/to/cuelet
meson setup apps/linux/build/debug apps/linux --wipe \
  --buildtype=debug -Dwerror=true
meson compile -C apps/linux/build/debug
meson test -C apps/linux/build/debug --print-errorlogs
./apps/linux/build/debug/cuelet
```

Release identity can be checked without opening a window:

```bash
./apps/linux/build/debug/cuelet --version
# Cuelet 0.1.0
```

The portal associates host applications with an installed desktop ID. For
desktop-wide shortcuts and a stable command path, use a user-local install:

```bash
meson setup apps/linux/build/install apps/linux --wipe \
  --buildtype=release -Dwerror=true --prefix="$HOME/.local"
meson compile -C apps/linux/build/install
meson install -C apps/linux/build/install
$HOME/.local/bin/cuelet
```

The installed `io.cuelet.Cuelet.desktop` file is required for host-app portal
registration. A direct build-tree launch remains suitable for local-only
development after that desktop entry has been installed.

The tracked `data/io.cuelet.Cuelet.svg` remains the canonical artwork. Meson
generates the installed Linux icon from it by changing only the root SVG
viewport to add transparent optical padding; all paths, transforms, styles,
and colors remain byte-for-byte unchanged.

## Command Line, Portal Shortcuts, and GNOME Fallback

Cuelet forwards commands to the running application instance. Useful commands include:

```bash
cuelet --list-sounds
cuelet --list-sounds --json
cuelet --list-categories --json
cuelet --play-id SOUND_ID
cuelet --play-name "Sound Name"
cuelet --play-file /path/to/sound.wav
cuelet --stop SOUND_ID
cuelet --stop-all
cuelet --show
cuelet --hide
cuelet --exit
cuelet --rescan
cuelet --library /path/to/library
```

Choose **Request Global** for a sound shortcut to use the standards-based XDG
portal. GNOME can show its own confirmation dialog. Cuelet does not claim that
the preferred key was accepted: cards and Preferences display the portal's
returned `trigger_description`, and distinguish pending, active, partially
approved, denied, unavailable, and disconnected states.

If the portal is unavailable or a shortcut is denied, local GTK handling stays
available while Cuelet is focused. The `command:` field from `--list-sounds`
can also be assigned manually in GNOME Settings → Keyboard → Custom Shortcuts:

```bash
cuelet --play-id <stable-sound-id>
```

The installed command forwards to an existing Cuelet instance. IDs—not names,
filenames, indexes, or view positions—select sounds. Invalid or deleted IDs
return a nonzero status and do not play another sound. Cuelet never edits GNOME
custom keybindings automatically.

## Cuelet Virtual Microphone

The Audio preferences page can create **Cuelet Virtual Microphone** as a
temporary stereo PipeWire input. It requires no kernel driver, root service,
default-device change, or file under `~/.config/pipewire` or
`~/.config/wireplumber`. PipeWire handles format negotiation, channel mapping,
and sample-rate conversion.

Available routing modes are:

- **Speakers only** — the feature is off and Cuelet uses its normal selected
  output.
- **Virtual microphone only** — Cuelet sounds target only the app-owned virtual
  input.
- **Speakers and virtual microphone** — independent GStreamer players send the
  sound to the normal output and the exact virtual input.

The optional **Mix Physical Microphone into Virtual Microphone** switch opens
only the explicitly selected PipeWire source. Cuelet excludes monitor sources,
its own nodes, virtual sources, unavailable devices, and duplicate stable
names. If that exact source disappears, soundboard injection continues in a
degraded state and Cuelet reconnects only when the same stable source returns.
The normal playback, virtual soundboard, and physical microphone levels are
controlled separately; both mix inputs default to 25% to leave headroom.
Routing and microphone-mix preferences are saved. If the user leaves the
feature enabled, a later Cuelet launch restores it and can reopen the same
selected microphone; disabling the switch before exit prevents that capture.

Select **Cuelet Virtual Microphone** inside the receiving application. Cuelet
does not change the desktop's default source. Cuelet must remain running: the
owned nodes and helper processes are removed when the feature is disabled or
the process exits. Parent-death handling also terminates owned helpers after an
unexpected Cuelet exit. Enabling physical microphone mixing never adds
sidetone, but an open microphone can acoustically pick up speakers; headphones
are recommended because Cuelet does not implement echo cancellation.

Useful non-destructive diagnostics are:

```bash
wpctl status -n
pw-dump | grep -E 'cuelet\.(virtual-microphone|soundboard-input|microphone-mix)'
pgrep -a pw-loopback
```

`Off`, `Starting`, `Ready`, `Degraded: selected microphone unavailable`,
`PipeWire unavailable`, `Failed`, and `Reconnecting` are shown in Preferences.
If the source does not appear, verify that `pw-loopback`, `pipewiresink`, and
the user's PipeWire session are available. Cuelet itself does not record the
microphone to disk or transmit audio over the network.

Run tests:

```bash
meson test -C apps/linux/build/debug --print-errorlogs
```

## Release Archive

No distro-specific Linux package is established yet. Create the conservative
binary archive from a fresh, stripped Meson Release build and its audited
`DESTDIR` installation tree:

```bash
./apps/linux/scripts/package-linux-release.sh
```

The command runs all Linux tests and validators before writing
`apps/linux/dist/Cuelet-0.1.0-linux-<architecture>.tar.gz`, where the
architecture is reported by `uname -m`. The archive contains only the
conventional `/usr` runtime tree: `cuelet`, its desktop entry, the scalable
hicolor icon, AppStream metadata, installation notes, installed-file manifest,
and license. It does not bundle normal system libraries.

On Ubuntu, the archive validator additionally uses `desktop-file-utils`,
`appstream`, `libxml2-utils`, `binutils`, `file`, `tar`, and `gzip`.

For byte-for-byte reproduction outside a Git checkout, pass the source commit's
timestamp explicitly:

```bash
SOURCE_DATE_EPOCH="$(git show -s --format=%ct HEAD)" \
  ./apps/linux/scripts/package-linux-release.sh /path/to/output
```

See the archive's `usr/share/doc/cuelet/README.md` for system-wide and
user-local installation instructions. The installed desktop identity is
`io.cuelet.Cuelet`, and the icon is installed as
`share/icons/hicolor/scalable/apps/io.cuelet.Cuelet.svg`.

Warnings-as-errors Debug and optimized Release builds:

```bash
meson setup apps/linux/build/debug apps/linux --wipe \
  --buildtype=debug -Dwerror=true -Ddeveloper_tools=true
meson compile -C apps/linux/build/debug
meson test -C apps/linux/build/debug --print-errorlogs

meson setup apps/linux/build/release apps/linux --wipe \
  --buildtype=release -Dwerror=true
meson compile -C apps/linux/build/release
meson test -C apps/linux/build/release --print-errorlogs

meson setup apps/linux/build/sanitize apps/linux --wipe \
  --buildtype=debug -Dwerror=true -Db_sanitize=address,undefined
meson compile -C apps/linux/build/sanitize
ASAN_OPTIONS=detect_leaks=0:halt_on_error=1 \
UBSAN_OPTIONS=halt_on_error=1 \
meson test -C apps/linux/build/sanitize --print-errorlogs
```

`developer_tools` is disabled by default and should remain off for Release
builds. Enabling it compiles the environment-driven visual-capture support used
to update approved documentation screenshots.

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
- XDG Desktop Portal GlobalShortcuts sessions with stable sound IDs, restored
  bindings, actual-trigger display, partial-approval handling, and debounced
  version-1 session replacement; GNOME custom-command and local GTK fallbacks
  remain available.
- GStreamer playback with pause/resume, progress, cached duration, single-sound stop, Stop All, simultaneous playback, volume, explicit PipeWire/PulseAudio target selection, cleanup, and a native mini-player.
- Single-instance command forwarding, show/hide/exit lifecycle, clean playback shutdown, and a GNOME notification when hidden playback starts.
- An app-managed PipeWire virtual microphone with speakers-only,
  virtual-only, and simultaneous speaker/virtual routing; optional exact-source
  physical microphone mixing; separate levels; stable public node names;
  hot-unplug recovery; scoped child ownership; and no default-device or
  persistent-configuration changes.
- GNOME-style preferences dialog sections for Library, Playback, Audio, Shortcuts, Appearance, Import Behavior, and Advanced.
- Keyboard navigation and screen-reader labels/selection state on sound cards and rows.

## Current Limitations

- Desktop-wide shortcuts exist only while Cuelet's portal session is alive.
  They continue while the window is hidden with `cuelet --hide`, but closing the
  window exits Cuelet and removes the session. There is no tray/background
  service. Portal availability, approval UI, and accepted key combinations are
  controlled by the desktop; GNOME Wayland 50.1 with portal interface version 1
  is the only desktop/session combination tested so far.
- Stock GNOME has no reliable built-in StatusNotifierItem tray surface. Cuelet supports single-instance show/hide/exit and notifications, but it exits when its window is closed and does not promise tray/background behavior.
- The virtual microphone was runtime-tested only on the native Ubuntu 26.04
  GNOME 50.1 Wayland PipeWire session. `pw-record` captured Cuelet-only and
  controlled mixed signals, and PipeWire graph inspection verified simultaneous
  speaker routing. GNOME Sound Recorder does not expose source selection, and
  Discord, OBS, browsers, games, KDE, Flatpak, and Snap were not tested, so
  compatibility with them is not claimed.
- Multiple simultaneous sounds and a physical microphone can still sum above
  the conservative default headroom. Cuelet does not add a compressor, limiter,
  acoustic echo cancellation, or sidetone.
- Explicit output selection accepts a current PipeWire `target-object` or PulseAudio device identifier; Cuelet does not yet enumerate friendly device names.
- Link imports approve the exact external file in per-user settings. External paths found only in portable library metadata remain unavailable until the user explicitly imports them; this prevents a copied metadata file from causing automatic reads outside the library.
- Duration metadata is discovered through GStreamer where possible. Files/codecs without discoverer support fall back to `--:--`.
- **Remove from Library** removes only the Cuelet metadata/view entry and never
  deletes audio. A later rescan can show an unchanged managed file again.
  Available managed sounds also expose a separate **Delete Managed File** action
  with an explicit destructive confirmation. Linked external files and missing
  entries never expose that deletion action.
- Import drag-in is implemented; file drag-out is not.
- The macOS SwiftUI app is unchanged. It still stores its app settings in Application Support; the Linux app writes the shared in-library metadata schema documented in `docs/metadata-schema.md` for future macOS adoption.

## Manual Desktop Checks

The service tests exercise import, persistence, playback state, path safety,
and removal execution. The following interactions still need a person to click
them in a real GNOME Wayland session before a release:

1. Toggle grid/list and confirm keyboard focus remains visible.
2. Import multiple files and a directory through the file dialog in both copy
   and link modes; also drag files into the window.
3. Click pause, resume, Stop, Stop All, and the volume control while listening
   to the selected output.
4. Toggle a favorite, edit a category color/icon, rename a managed sound, and
   restart Cuelet to confirm the resulting presentation.
5. Use Reveal in Files and a local shortcut from the focused window. Portal
   shortcuts were separately verified with Ptyxis, Files, and Cuelet focused.
6. Cancel both removal dialogs, then verify Remove from Library preserves the
   file and Delete Managed File removes only the confirmed managed file.
