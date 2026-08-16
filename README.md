# Cuelet

Cuelet is a native cross-platform desktop soundboard for organizing, searching, and playing audio clips quickly during calls, streams, tabletop sessions, voice chat, and live cues.

It is built from scratch with Qt 6, CMake, C++, Qt Multimedia, and CTest. It is not Electron and it is not a wrapper around a script.

## Features

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

<table>
  <tr>
    <th width="68%">macOS — Library</th>
    <th width="32%">macOS — About Cuelet</th>
  </tr>
  <tr>
    <td><img src="docs/images/cuelet-main-macos.png" alt="Cuelet library on macOS with five healthy sound cards"></td>
    <td><img src="docs/images/cuelet-about-macos.png" alt="Branded native About Cuelet window on macOS"></td>
  </tr>
</table>

### Windows

| Sound library | About Cuelet |
| --- | --- |
| ![Cuelet sound library on Windows](docs/images/cuelet-main-windows.png) | ![About Cuelet on Windows](docs/images/cuelet-about-windows.png) |

## Dependencies

- CMake 3.21 or newer
- C++17 compiler
- Qt 6 with these modules:
  - Core
  - Gui
  - Widgets
  - Multimedia
  - Test

Qt Multimedia uses platform media backends. Codec support can vary by OS and Qt installation, especially for compressed formats such as `mp3`, `m4a`, and `flac`.

## Build

The main build path is:

```bash
cmake -S . -B build
cmake --build build
```

On macOS this creates `build/Cuelet.app`. On Windows it creates a `Cuelet.exe` target. On Linux it creates a `Cuelet` executable.

## Linux

Install Qt 6 development packages with your distribution package manager. Package names vary:

```bash
sudo apt install cmake g++ qt6-base-dev qt6-multimedia-dev qt6-tools-dev
cmake -S . -B build
cmake --build build
```

For Fedora-style systems, install the equivalent Qt 6 base, multimedia, and tools development packages.

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

## Tests

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
- Linux: CMake builds a normal desktop executable; AppImage and Flatpak packaging can be added later.
- Settings are stored with `QSettings`, so their location follows the host OS conventions.
- Library metadata is stored as JSON inside the selected library folder so it survives rescans and can travel with the audio folder.

## Settings

Cuelet stores application settings with Qt `QSettings`. The exact path depends on the operating system and Qt backend, and the Settings screen shows the active settings file path.

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

## Known Limitations

- The UI is a solid first Qt Widgets version, not a final design system.
- Metadata is JSON rather than SQLite. This keeps v1 simple and inspectable, but very large libraries may eventually benefit from indexing.
- Playback depends on Qt Multimedia and the platform's installed codec support.
- Waveform previews, hotkey assignment per sound, global shortcuts, tags, and playlists are not implemented yet.
- Output-device changes affect newly started playback.
- There is no installer packaging yet.
- Loudness normalization is a stored compatibility setting but is not active in playback yet.
- Legacy virtual microphone settings are preserved but not implemented as routing functionality.

## Virtual Microphone Notes

Cuelet includes an audio service boundary that can later support routing strategies, but v1 does not fake virtual microphone support.

- Linux may later integrate with PipeWire or PulseAudio routing.
- macOS generally needs a virtual device such as BlackHole.
- Windows generally needs a virtual cable device such as VB-Cable.

See `docs/virtual-microphone.md` for the current design notes.

## Future Roadmap

- Capture screenshots and refine per-platform visual polish.
- Add waveform display and clip trimming.
- Add configurable per-sound keyboard shortcuts.
- Add global hotkeys with platform-specific permission handling.
- Add richer import workflows and duplicate detection.
- Add AppImage, Flatpak, signed macOS app, and Windows installer packaging.
- Add a virtual microphone/routing backend where the platform supports it cleanly.
- Add SQLite or search index support if JSON metadata becomes a bottleneck.
