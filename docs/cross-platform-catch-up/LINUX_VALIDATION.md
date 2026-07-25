# Linux validation record

This record covers the native GTK4/libadwaita catch-up on
`feat/linux-parity-catch-up`. Validation used only generated audio fixtures
and isolated XDG settings.

## Environment

| Item | Observed value |
|---|---|
| Repository | `/home/oki/projects/cuelet` |
| Base commit | `5f7fb0ef76f16689bd5cdde324c063c559526a28` |
| CPU | `x86_64` |
| OS | Ubuntu 26.04 LTS |
| Desktop | GNOME 50.1, Wayland |
| GTK / libadwaita | 4.22.4 / 1.9.1 |
| PipeWire / WirePlumber | 1.6.2 / 0.5.13 |
| GStreamer / JSON-GLib | 1.28.2 / 1.10.8 |
| CMake / Meson / Ninja | 4.2.3 / 1.10.1 / 1.13.2 |
| Compiler | GCC/G++ 15.2.0 |

The development headers were absent from the system package database. A
reported `sudo apt install` attempt stopped at the password prompt and made no
change. Required Ubuntu packages were downloaded and extracted without
privilege under `/tmp/cuelet-sdk-ubuntu2604`; no package, credential, desktop,
default-device, or persistent PipeWire configuration was installed or
modified.

## Build and automated test results

The native Linux build used this SDK search path on this machine:

```bash
export PKG_CONFIG_PATH=/tmp/cuelet-sdk-ubuntu2604/root/usr/lib/x86_64-linux-gnu/pkgconfig
meson setup /tmp/cuelet-linux-debug-final.suxnrA apps/linux --wipe \
  -Dbuildtype=debug -Dwerror=true
meson compile -C /tmp/cuelet-linux-debug-final.suxnrA
meson test -C /tmp/cuelet-linux-debug-final.suxnrA --print-errorlogs

meson setup /tmp/cuelet-linux-release-final.Nrb2tL apps/linux --wipe \
  -Dbuildtype=release -Dwerror=true
meson compile -C /tmp/cuelet-linux-release-final.Nrb2tL
meson test -C /tmp/cuelet-linux-release-final.Nrb2tL --print-errorlogs

meson setup /tmp/cuelet-linux-sanitize-final.zUh5Ns apps/linux --wipe \
  -Dbuildtype=debug -Dwerror=true -Db_sanitize=address,undefined
meson compile -C /tmp/cuelet-linux-sanitize-final.zUh5Ns
ASAN_OPTIONS=detect_leaks=0:halt_on_error=1 \
UBSAN_OPTIONS=halt_on_error=1 \
meson test -C /tmp/cuelet-linux-sanitize-final.zUh5Ns --print-errorlogs
```

All nine tests passed in strict Debug, optimized Release, and focused
ASan/UBSan builds:

- shared scanner/search/types;
- shared metadata persistence and malformed/missing inputs;
- CLI parsing/formatting;
- category helpers;
- validated XDG settings and linked-file approvals;
- copy/link/drop import planning and execution;
- GStreamer state, progress, output, and cleanup;
- PipeWire command/config generation;
- PipeWire child ownership, rollback, diagnostics, and cleanup.

`gcovr` 8.6 reported 81% line coverage and 47% branch coverage across the
scoped shared-core and Linux service/helper logic. GTK rendering and
desktop-session callbacks were validated at runtime rather than counted in
that scoped percentage.

The older root Qt/CMake reference also built in clean Debug and Release
directories and passed all six CTest tests in each configuration. Warnings
were enabled. A stricter `-Werror` probe exposed three pre-existing warnings
in the unrelated Qt prototype (`LegacySettingsImporter.cpp` and
`MainWindow.cpp`); those files were not changed by the Linux catch-up.

## Wayland application validation

The real Release GTK application ran under the active GNOME Wayland session
with `G_DEBUG=fatal-warnings`. A single instance handled show, hide, play,
stop-all, show, and exit commands; the originating process exited with status
zero and no Cuelet or playback process remained.

Generated WAV fixtures exercised managed, linked, missing, favorite, recent,
category, search, duration, and nested-folder states. An isolated settings
profile explicitly approved the linked fixture. Imports bind the selected
file identity across planning/execution, managed copies use no-follow and
exclusive descriptor operations, and playback/duration retain a nonblocking
no-follow regular-file descriptor through GStreamer's `/proc/self/fd` URI.
A separate profile with no
approval loaded the same portable metadata: the external link had an empty
runtime path and playback failed as missing, demonstrating that metadata
alone cannot make Cuelet read an arbitrary external file.

Eleven PNGs under [linux-screenshots](linux-screenshots/README.md) were
regenerated from the real GTK widget tree, opened as a contact sheet for
visual inspection, and validated with `file` and ImageMagick `identify`.
GNOME denied compositor-level capture, and the application renderer cannot
include a separate Wayland popup surface, so no context-menu screenshot is
claimed.

## PipeWire safety validation

The optional route was enabled against the live user-session PipeWire graph.
`pw-dump` and `wpctl` showed the temporary Cuelet sink and source only while
the owned `pw-loopback` process was running. Cuelet sent a generated WAV to
the temporary sink; `pw-record` captured non-silent PCM from the corresponding
source, and `ffmpeg` analysis measured mean volume around -17.3 dB.

Default PipeWire metadata was captured before, during, and after the test and
was identical. Stop removed the exact child process and both temporary nodes.
No system or user PipeWire configuration was written, and no default sink or
source was changed. Discord and OBS were not installed, so receiving-app
compatibility is not claimed.

## Remaining environment limits

- GNOME Wayland global shortcuts need a packaged identity plus an explicit
  xdg-desktop-portal consent/session and restore-token implementation.
- Stock GNOME provides no reliable built-in tray contract.
- The temporary virtual route does not yet mix a physical microphone or
  monitor simultaneously through local speakers.
- Friendly PipeWire device enumeration, drag-out, packaging, and formal
  AT-SPI testing remain application/packaging follow-up work.
