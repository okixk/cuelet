# Linux catch-up status

Validated on native Ubuntu 26.04 GNOME/Wayland (`x86_64`) with GTK 4.22.4,
libadwaita 1.9.1, PipeWire 1.6.2, GStreamer 1.28.2, CMake 4.2.3, and GCC
15.2.0. The machine did not have the GTK/libadwaita development packages
installed system-wide, so builds used an unprivileged SDK extracted under
`/tmp`; no package database or system configuration was changed.

## Completed vertical slices

1. **Shared behavior/data model:** shared scanner/search/types and schema-v2
   metadata now cover favorites, recent plays, categories, linked sounds,
   missing managed sounds, and duration fingerprints. Metadata writes are
   atomic and malformed/legacy input is tolerated.
2. **Navigation/library UI:** the native GTK4/libadwaita app implements
   populated, empty, missing-folder and no-results states, grid/list modes,
   responsive split navigation, sorting, focus, selection, keyboard menus,
   and accessibility labels/state.
3. **Categories/search/playback:** category CRUD/color/icon mapping,
   assignment, ranked search, Enter/Escape behavior, pause/resume,
   progress/duration, overlap policy, output target selection, and the
   mini-player are working.
4. **Import and management:** file/folder dialogs and `GdkFileList` drop
   import use a tested copy/link service. Collision, duplicate, traversal,
   symbolic-link rejection, missing-file recovery, partial failure, safe
   removal planning, managed rename, and linked display-name behavior are
   explicit. External paths copied in library metadata stay unavailable until
   the user imports and approves that exact file.
5. **Settings/lifecycle:** validated XDG JSON settings, system appearance,
   single-instance forwarding, show/hide/exit commands, hidden-playback
   notifications, and exact playback/routing cleanup are implemented.
6. **PipeWire routing:** the Audio preferences page can create a temporary
   Cuelet-owned virtual sink/source route and target Cuelet playback at its
   sink. The runtime uses direct argv, retains exact child handles, rolls back
   partial starts, bounds shutdown, and never changes defaults or writes
   PipeWire configuration.
7. **Tests/validation:** nine Meson test targets cover the shared core and
   Linux services. Debug, Release, ASan/UBSan, live Wayland workflows,
   temporary PipeWire routing, and application-rendered PNG states are part
   of the [validation record](LINUX_VALIDATION.md).

## Remaining gaps and exact blockers

| Item | Current status | Exact blocker | Attempted approach | Recommended next step | Classification |
|---|---|---|---|---|---|
| Portal global shortcuts | Local capture/conflict handling and GNOME custom-shortcut commands work | GNOME Wayland forbids unrestricted global grabs; portal registration requires a consent/session flow, packaged app identity, and durable restore-token handling | Verified the desktop exposes the GlobalShortcuts portal; retained safe GNOME Settings command integration | Implement the xdg-desktop-portal GlobalShortcuts session API behind explicit user consent and test packaged/unpackaged restore behavior | Wayland / desktop / packaging |
| Tray/background menu | Single-instance show/hide/exit and notifications work | Stock GNOME does not reliably expose StatusNotifierItem without an extension, and libadwaita has no native tray abstraction | Inspected the session watcher and kept lifecycle usable without a tray dependency | Add an optional StatusNotifierItem backend only with KDE and GNOME-extension test coverage; keep window-close exit as the reliable default | Desktop |
| Virtual route plus local speakers | Virtual microphone route works, but Cuelet playback is routed exclusively to it while active | Safe simultaneous monitoring needs a GStreamer tee or explicit PipeWire links, clocking/resampling, device-loss handling, and loop prevention | Kept desktop defaults untouched; kept ordinary local output selection independent; validated non-silent audio through the temporary source | Add a tested tee/mixer graph with separate virtual and physical branches plus opt-in local monitoring | Application / PipeWire |
| Physical microphone mixing | Not implemented | Requires capture-device enumeration, permission/error UX, gain/mute controls, resampling, echo/loop avoidance, and privacy review | No physical microphone was opened or modified during catch-up | Add a separate PipeWire capture/mixer service with fake-graph tests before any live opt-in test | Application / PipeWire |
| Discord/OBS compatibility | Not claimed | Discord and OBS were not installed in the validation environment | Verified the source with `pw-record` and analyzed a generated non-silent WAV; inspected node presence and cleanup | Test selection, audio flow, reconnect, and teardown in each receiving application on a disposable user session | Missing test environment |
| Friendly device enumeration | Explicit PipeWire/PulseAudio target identifiers work | Current UI has no endpoint model for stable IDs, human names, hotplug, and unavailable-device recovery | Validated backend/plugin availability and safe per-Cuelet target assignment | Build a read-only PipeWire registry model and bind it to the native dropdown | Application / PipeWire |
| Import drag-out | Drag-in works | GTK content-provider/file drag source has not been implemented or tested with GNOME Files | Implemented and tested `GdkFileList` drop ownership and copy/link import | Add a per-card `GtkDragSource` exposing local file lists without move/delete actions | Application |
| Context-menu screenshot | Behavior is implemented and reviewed; no Linux PNG is claimed | GNOME Shell rejected external window capture and the application renderer cannot include a separate Wayland popup surface | Opened the native popover; rejected and removed a PNG that showed only the selected card | Capture interactively through an approved compositor/portal surface and validate the popup is visible before committing | Wayland / missing test environment |
| Packaging | Source builds are validated | No Flatpak/AppImage manifest, desktop file, signing, or portal permission policy exists | Kept runtime/development dependencies documented and used no privileged install | Add desktop metadata and a least-privilege Flatpak manifest, then repeat notification/portal/PipeWire tests inside the sandbox | Packaging |

## Audio safety result

The live PipeWire check recorded default metadata before, during, and after
the temporary route. The Cuelet nodes appeared only while the exact
`pw-loopback` child was owned, a generated WAV crossed the virtual route with
non-silent output, and both nodes and the process disappeared on stop. Default
sink/source metadata was unchanged. No global or per-user PipeWire
configuration file was written.
