# Cuelet

Cuelet is a native cross-platform desktop soundboard for organizing,
searching, and playing audio clips during calls, streams, tabletop sessions,
voice chat, and live cues.

![Cuelet soundboard on Linux](docs/images/cuelet-main-linux.png)

The native Linux frontend uses GTK4/libadwaita and integrates with GStreamer
and PipeWire for responsive playback and virtual-microphone routing.

![About Cuelet](docs/images/cuelet-about-linux.png)

Each supported platform has a native frontend:

- Linux: GTK4/libadwaita, C++, GStreamer, PipeWire integration, and Meson
  under `apps/linux`.
- macOS: SwiftUI/AppKit under `apps/macos`.
- Windows: WinUI 3 and C++/WinRT under `apps/windows`.

Toolkit-neutral C++ models, scanning, search, and metadata behavior live under
`core/cuelet-core`.

## Linux

The production Linux frontend targets Ubuntu/GNOME/Wayland. Install its build
dependencies on Ubuntu:

```bash
sudo apt install build-essential meson ninja-build pkg-config \
  libgtk-4-dev libadwaita-1-dev \
  libgstreamer1.0-dev libgstreamer-plugins-base1.0-dev \
  libjson-glib-dev
```

Build and test:

```bash
meson setup apps/linux/build/debug apps/linux \
  --buildtype=debug -Dwerror=true
meson compile -C apps/linux/build/debug
meson test -C apps/linux/build/debug --print-errorlogs
./apps/linux/build/debug/cuelet
```

See [apps/linux/README.md](apps/linux/README.md) for runtime codecs, PipeWire
virtual-microphone behavior, GNOME/Wayland shortcuts, installation, and the
reproducible `0.1.0` release archive workflow.

## macOS

See [apps/macos/README.md](apps/macos/README.md) for the native macOS build,
test, virtual-audio driver, and package workflows.

## Windows

See [apps/windows/README.md](apps/windows/README.md) for the native Windows
build, test, virtual-audio driver, and package workflows.

## Project Structure

```text
apps/
  linux/
  macos/
  windows/
core/
  cuelet-core/
docs/
VERSION
LICENSE
```

See [docs/architecture.md](docs/architecture.md) for module and build
boundaries.

## Shared Data

Library metadata is stored in `.cuelet-metadata.json` inside the selected
library so it survives rescans and can move with the audio folder. See
[docs/metadata-schema.md](docs/metadata-schema.md) for the shared schema and
legacy-data migration behavior.

Application settings and audio routing remain platform-specific. Linux stores
settings atomically in the XDG configuration directory and provides temporary
PipeWire virtual-microphone routing without changing the desktop default
source.
