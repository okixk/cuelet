# Linux validation record

> Historical note: references below to the former root Qt/CMake prototype
> describe the repository as it existed during the July 2026 validation. That
> prototype was removed before the Linux 0.1.0 final audit.

This record covers the native GTK4/libadwaita catch-up on
`feat/linux-parity-catch-up`. Validation used only generated audio fixtures
and isolated XDG settings.

## 2026-07-31 Cuelet Virtual Microphone validation

The native client now owns a transient stereo PipeWire graph for **Cuelet
Virtual Microphone**. One directly executed `pw-loopback` exposes stable
`cuelet.soundboard-input` (`Audio/Sink`) and `cuelet.virtual-microphone`
(`Audio/Source`) nodes. GStreamer targets only that exact sink for Cuelet sound
injection. An optional second owned `pw-loopback` connects only the selected
physical source to `cuelet.soundboard-input`; it is never connected to the
speaker output. All helpers use direct argument vectors, per-process handles,
bounded stderr capture, `object.linger=false`, disabled session-property
restoration, and Linux parent-death signalling. No kernel module, service,
PipeWire/WirePlumber configuration, default-device change, or shell command is
used.

Runtime validation used `/tmp/cuelet-vmic-jKqxkK` for XDG settings, library
metadata, generated WAVs, and recordings. `wpctl status -n` and `pw-dump`
independently observed the virtual source and internal sink. `pw-record`
selected `cuelet.virtual-microphone` by stable name. A generated Cuelet 1 kHz
fixture was captured from the source; with the physical mix disabled, an
unrelated 440 Hz desktop-output test was more than 34 dB below the Cuelet tone,
showing that desktop audio was not captured.

The soundboard level regression check compared the source WAV with a 25%
virtual level. The source peak of -1.94 dB became -13.98 dB in the virtual
recording, the expected 12.04 dB attenuation. A controlled test-only physical
source then injected 440 Hz while Cuelet injected 1 kHz. At the 25% defaults,
the final virtual recording measured 440 Hz at -22.00 dB RMS and 1 kHz at
-21.83 dB RMS, with a -9.33 dB peak and no clipping. No ambient speech was
intentionally recorded.

Virtual-only mode linked the Cuelet stream only to the owned sink. Combined
mode used independent GStreamer players; live graph inspection showed one
Cuelet stream linked to the normal physical speaker and another stable
`cuelet.soundboard-output` stream linked to the virtual sink. The speaker path
was graph-verified, not audibly judged by automation. PipeWire performed the
final mix and format conversion. Cuelet never monitored the completed virtual
source back to speakers.

Stopping and restarting the controlled physical source removed the physical
mix helper while leaving sound injection operational, then recreated that
helper only when the same stable `node.name` returned with a new numeric ID and
description. Terminating an owned physical helper caused one controlled
replacement. Repeated enable/disable cycles removed the exact helpers and
nodes, and process exit also removed them. The configured default sink and
source were identical before and after validation. A live PipeWire service
restart was not attempted because it would disrupt the user's audio session.

The receiving client used for signal validation was PipeWire's `pw-record`.
GNOME Sound Recorder 43.beta was installed but does not expose an input-source
selector, so it could not safely select Cuelet without changing the desktop
default. No default was changed. Discord, OBS, Firefox capture, games, KDE,
Flatpak, and Snap were not tested and are not claimed.

After the final lifecycle and UI fixes, clean warnings-as-errors Debug and
Release builds completed and all 12 registered Meson tests passed in both.
The same 12 tests passed under combined ASan/UBSan with leak detection disabled
for GLib/GStreamer process-global allocations and fatal sanitizer errors
enabled. `git diff --check` passed. A final post-build AT-SPI check observed the
degraded state while the saved exact microphone was absent, then used the
Preferences Refresh action after that source returned; the renamed device was
listed, sensitive, and reconnected without selecting another source.

## 2026-07-31 XDG GlobalShortcuts validation

Cuelet now uses a dedicated asynchronous GDBus peer for the official
`org.freedesktop.portal.GlobalShortcuts` interface. The dedicated connection is
important for an unsandboxed GTK application: it registers
`io.cuelet.Cuelet` through `org.freedesktop.host.portal.Registry` before making
any other portal call. A user-local Meson install supplied the matching
`~/.local/share/applications/io.cuelet.Cuelet.desktop` and stable
`~/.local/bin/cuelet` path. No GNOME keybindings were changed.

The live GNOME 50.1 Wayland session exposed portal interface version 1 through
xdg-desktop-portal 1.21.1 and xdg-desktop-portal-gnome 50.0. The initial bind
opened GNOME's **Add Keyboard Shortcuts** confirmation for two generated WAVs.
The accepted triggers returned by the portal were `Press <Control><Alt>9` and
`Press <Control><Alt>0`; Cuelet displayed those returned descriptions rather
than its preferred-trigger strings. Disabling a third shortcut in the GNOME
dialog produced a partial result, a visible denied state, and no global
activation for that key.

Compositor-delivered keys were injected through a temporary `ydotool` uinput
device; AT-SPI verified which real Wayland window was active. With each of
Ptyxis, GNOME Files, and Cuelet focused, `Ctrl+Alt+9` played only stable ID
`11111111-1111-4111-8111-111111111111`, while `Ctrl+Alt+0` played only stable ID
`22222222-2222-4222-8222-222222222222`. Both worked repeatedly. Search
filtering and grid/list switching did not alter the target. After the managed
fixture was renamed, the same ID and binding played the renamed sound. Removing
another sound from the isolated model made its old ID inert without crashing
while the version-1 replacement session was debounced.

Closing and restarting Cuelet removed and recreated the live session. GNOME
reused the accepted bindings on the replacement BindShortcuts call without a
second confirmation; its version-1 ListShortcuts response was empty before that
bind. `cuelet --hide` kept the process and portal shortcut active, while closing
the window with compositor-delivered Alt+F4 exited the process and made the key
inert. The fallback command forwarded correctly to the resident instance:
`cuelet --play-id 22222222-2222-4222-8222-222222222222` returned zero and
played that exact sound; a deleted/invalid ID returned one with a clear error.

AT-SPI inspection also confirmed active, partial, and denied status text in
Preferences. Portal-returned angle-bracket trigger descriptions are escaped
before being passed to libadwaita markup APIs. The accessibility client package
was downloaded and extracted under `/tmp` because the attempted system install
required authentication; no package was installed for this check.

At that global-shortcut checkpoint, clean Debug and Release builds completed
with GCC 15.2.0 and `-Dwerror=true`. All 11 then-registered Meson tests passed
in both configurations, including the injectable
portal-controller suite, and `git diff --check` passed.

## 2026-07-31 baseline rerun and removal catch-up

The native client was revalidated from `ed49d8264893` on Ubuntu 26.04 LTS
x86_64, GNOME 50.1, and Wayland. System installation of the five missing
development packages reached an authentication prompt and was cancelled; no
system package state changed. The exact apt-resolved development package set
was instead downloaded and extracted under `/tmp/cuelet-sdk-ubuntu2604`, then
used through `PKG_CONFIG_PATH`. Qt was not installed or used.

Strict Debug and optimized Release builds completed with warnings treated as
errors. All nine Meson tests passed in Debug, Release, and combined ASan/UBSan
builds. `git diff --check` also passed.

The rebuilt Release application ran in the active Wayland session with
`G_DEBUG=fatal-warnings` and isolated XDG settings. Generated WAV fixtures
confirmed one-instance command forwarding, grid/search/empty/playback
rendering, persisted library selection, cached durations, a live PipeWire
output stream, stop/exit cleanup, missing-entry retention and recovery, and
safe rejection of unsupported and symlink media. Damaged media reported an
error without terminating the resident application. File-dialog import,
list-toggle clicks, pause/resume clicks, favorites, category editing, reveal,
and local-shortcut capture remain explicit manual desktop checks.

Removal behavior now distinguishes metadata-only removal from an explicitly
confirmed managed-file deletion. The executor revalidates a non-symlink
regular file and its device/inode identity immediately before deletion.
Focused tests prove that metadata-only removal preserves managed files, linked
external sources are never deleted, changed/symlink paths are rejected, and a
confirmed managed deletion removes only its planned library file. A real GTK
capture rendered the destructive confirmation; closing it preserved the test
file.

A practical initial scan of 251 tiny WAV fixtures took 1.99 seconds and about
276 MB peak RSS in both Debug and Release; the cached rescan took 0.54 seconds.
Duration discovery still runs synchronously in `loadLibrary`, so large-library
responsiveness remains a confirmed follow-up.

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

All nine tests registered at that baseline checkpoint passed in strict Debug,
optimized Release, and focused
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

## Earlier PipeWire safety validation

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

- Stock GNOME provides no reliable built-in tray contract.
- A real PipeWire daemon restart was not performed because it would disrupt
  the active user audio session; disconnect/reconnect behavior is covered by
  the injectable controller test and helper/source reconstruction was tested
  live.
- An ordinary GUI recording client with an explicit input selector was not
  available. `pw-record` validated the signal path, but application-specific
  selection remains a manual compatibility check.
- Drag-out, broader packaging, and automated display-backed AT-SPI coverage
  remain follow-up work.
