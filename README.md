# Cuelet

Cuelet is a native cross-platform desktop soundboard for organizing, searching, and playing audio clips quickly during calls, streams, tabletop sessions, voice chat, and live cues.

Cuelet now has native platform frontends. The production Linux app is built
with GTK4/libadwaita, C++, GStreamer, PipeWire integration, and Meson. The Qt
6/CMake application at the repository root is retained as an earlier prototype.

## Legacy Qt Prototype Features

The following list describes the retained root-level prototype. Native
frontends document their current feature sets in their platform READMEs.

- Choose and remember a sound library folder.
- Recursively scan `mp3`, `wav`, `ogg`, `flac`, and `m4a` files.
- Search by title, filename, category, notes, and aliases.
- Filter by favorites and category.
- Play and stop sounds through Qt Multimedia.
- Optional multiple simultaneous playback.
- Volume control and audio output device selection.
- Display controls for file-extension visibility and persisted sidebar width.
- Preserved loudness-normalization preference for future backend support.
- Import/copy audio files into the active library.
- Drag and drop supported audio files into the app.
- Edit per-sound metadata:
  - display title
  - category
  - favorite status
  - icon or emoji text
  - notes
  - aliases/search keywords
- Persist metadata in `.cuelet-metadata.json` inside the selected library.
- Persist app settings with Qt's platform settings APIs.
- Keep metadata for files that disappear and mark them as missing.
- Best-effort legacy soundboard settings import from old JSON/CONF config files.
- Handle missing folders, invalid metadata, unsupported files, and unusual filenames without crashing.

## Screenshots

Screenshots will be added once the first visual pass is captured on each target platform.

## Legacy Qt Prototype Dependencies

- CMake 3.21 or newer
- C++17 compiler
- Qt 6 with these modules:
  - Core
  - Gui
  - Widgets
  - Multimedia
  - Test

Qt Multimedia uses platform media backends. Codec support can vary by OS and Qt installation, especially for compressed formats such as `mp3`, `m4a`, and `flac`.

## Legacy Qt Prototype Build

The root prototype build path is:

```bash
cmake -S . -B build
cmake --build build
```

On macOS this creates `build/Cuelet.app`. On Windows it creates a `Cuelet.exe`
target. The root prototype can also create a Linux `Cuelet` executable, but the
supported native Linux release uses the Meson workflow below.

## Linux

The production Linux frontend is under `apps/linux`. On Ubuntu, install:

```bash
sudo apt install build-essential meson ninja-build pkg-config \
  libgtk-4-dev libadwaita-1-dev \
  libgstreamer1.0-dev libgstreamer-plugins-base1.0-dev \
  libjson-glib-dev
meson setup apps/linux/build/debug apps/linux --buildtype=debug -Dwerror=true
meson compile -C apps/linux/build/debug
meson test -C apps/linux/build/debug --print-errorlogs
```

See [`apps/linux/README.md`](apps/linux/README.md) for runtime dependencies,
PipeWire virtual-microphone behavior, GNOME/Wayland shortcuts, installation,
and the reproducible `0.1.0` release archive workflow.

## macOS

With Homebrew:

```bash
brew install cmake qt
cmake -S . -B build -DCMAKE_PREFIX_PATH="$(brew --prefix qt)"
cmake --build build
open build/Cuelet.app
```

## Windows

One practical path is MSYS2 with MinGW Qt 6:

```bash
pacman -S --needed mingw-w64-ucrt-x86_64-cmake mingw-w64-ucrt-x86_64-gcc mingw-w64-ucrt-x86_64-qt6-base mingw-w64-ucrt-x86_64-qt6-multimedia
cmake -S . -B build -G "MinGW Makefiles"
cmake --build build
```

You can also build with Visual Studio if Qt 6 is installed for the same compiler kit and `CMAKE_PREFIX_PATH` points at that Qt installation.

The native Windows frontend is under `apps/windows` and is independent of the Qt prototype. It uses WinUI 3 and C++/WinRT:

```powershell
powershell -ExecutionPolicy Bypass -File .\apps\windows\scripts\build-windows.ps1 -Configuration Debug
powershell -ExecutionPolicy Bypass -File .\apps\windows\scripts\run-windows.ps1 -NoBuild
powershell -ExecutionPolicy Bypass -File .\apps\windows\scripts\test-windows.ps1 -NoBuild
```

See `apps/windows/README.md` for native Windows requirements, features, and packaging notes.

## Legacy Qt Prototype Tests

```bash
cmake -S . -B build
cmake --build build
ctest --test-dir build --output-on-failure
```

Current CTest coverage includes:

- library scanning
- metadata save/load
- invalid metadata handling
- legacy settings import and favorite/metadata merging
- search/filtering
- path and missing-folder handling
- missing-file metadata preservation
- a core smoke test

## Project Structure

```text
CMakeLists.txt
README.md
src/
  main.cpp
  app/
  audio/
  core/
  platform/
  storage/
  ui/
tests/
resources/
docs/
```

See `docs/architecture.md` for the main module boundaries.

## Platform Notes

- macOS: CMake is configured to build `Cuelet.app`.
- Windows: CMake is configured for a GUI `Cuelet.exe`.
- Linux: Meson builds the native GTK executable `cuelet`; the `0.1.0` release is
  distributed as a reproducible archive of its staged `/usr` installation tree.
- The legacy Qt prototype stores settings with `QSettings`; the native Linux
  app uses an atomic JSON settings store in the XDG configuration directory.
- Library metadata is stored as JSON inside the selected library folder so it survives rescans and can travel with the audio folder.

## Legacy Qt Prototype Settings

The legacy prototype stores application settings with Qt `QSettings`. The
exact path depends on the operating system and Qt backend, and its Settings
screen shows the active settings file path. See `apps/linux/README.md` for the
native Linux settings and metadata behavior.

Current app settings include:

- remembered library folder
- volume
- allow multiple sounds at once
- show file extensions
- loudness normalization preference, preserved but not active yet
- sidebar width
- audio output device ID
- legacy import status and last import summary
- preserved legacy virtual microphone settings

Per-sound metadata is stored in `.cuelet-metadata.json` inside the selected sound library. Favorites are persisted there per relative sound path.

## Legacy Import

Cuelet includes a compatibility importer for old soundboard configs. It does not require the old project source code.

On first launch, Cuelet searches standard app config locations for likely files:

- `soundboard.json`
- `settings.json`
- `config.json`
- `soundboard.conf`

You can also import a legacy config manually from Settings.

Supported setting mappings:

- `library_dir` → Cuelet library folder
- `show_extensions` → show file extensions
- `use_loudness` → loudness normalization preference
- `sidebar_width` → persisted sidebar width
- `output_device` → best-effort match by Qt audio output device description
- `favorite_paths` → merged with Cuelet favorites
- `virtual_mic_enabled`, `mic_loopback_enabled`, `virtual_mic_output_device`, `virtual_mic_input_device` → preserved as inactive legacy status

Legacy metadata mappings:

- `title` → title
- `category` → category
- `note` / `notes` → notes
- `favorite` → favorite
- `icon` / `emoji` → icon
- `shortcut` and `link` → preserved as searchable aliases

Import is intentionally conservative. Existing Cuelet metadata wins, and missing fields are filled from legacy metadata. Legacy favorites are merged, not used to clear existing favorites. Absolute favorite paths are converted to relative paths only when they are inside the selected library; unsafe paths are skipped and recorded in the import summary.

## Legacy Qt Prototype Limitations

- These limitations describe the retained Qt prototype; native platform apps
  have their own README and validation records.
- The UI is a solid first Qt Widgets version, not a final design system.
- Metadata is JSON rather than SQLite. This keeps v1 simple and inspectable, but very large libraries may eventually benefit from indexing.
- Playback depends on Qt Multimedia and the platform's installed codec support.
- Waveform previews, hotkey assignment per sound, global shortcuts, tags, and playlists are not implemented yet.
- Output-device changes affect newly started playback.
- The legacy Qt prototype has no installer packaging; native frontends use
  their platform-specific release workflows.
- Loudness normalization is a stored compatibility setting but is not active in playback yet.
- Legacy virtual microphone settings are preserved but not implemented as routing functionality.

## Virtual Microphone Notes

Cuelet's native apps use platform-specific routing. The legacy Qt prototype
only contains an inactive audio-service boundary.

- Linux provides temporary PipeWire routing and works with the desktop's
  PulseAudio compatibility layer where available.
- macOS generally needs a virtual device such as BlackHole.
- Windows generally needs a virtual cable device such as VB-Cable.

See `docs/virtual-microphone.md` for the current design notes.

## Legacy Prototype Roadmap

- Capture screenshots and refine per-platform visual polish.
- Add waveform display and clip trimming.
- Add configurable per-sound keyboard shortcuts.
- Add global hotkeys with platform-specific permission handling.
- Add richer import workflows and duplicate detection.
- Evaluate distro-specific Linux packages after the portable staged-tree
  archive has been validated across additional distributions.
- Add a virtual microphone/routing backend where the platform supports it cleanly.
- Add SQLite or search index support if JSON metadata becomes a bottleneck.
