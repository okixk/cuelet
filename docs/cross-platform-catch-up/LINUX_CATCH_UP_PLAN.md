# Linux catch-up plan

1. **Shared behavior/data model (M, P0):** consume shared core for scan, types, search, metadata and settings fixtures; reconcile Linux category and favorite persistence. Targets `core/cuelet-core`, `apps/linux/src`, `apps/linux/tests`.
2. **Navigation/library UI (L, P0):** align GTK/libadwaita sidebar, onboarding, grid/list, sort, empty/missing states and keyboard focus. Targets `apps/linux/src/CueletWindow*`, `resources/style.css`. Acceptance: fixture-driven parity with Windows semantics.
3. **Categories/search/playback (L, P0):** implement category CRUD/color/icon mapping, ranked search/Enter play, mini-player and progress. Targets `CueletWindowWidgets.cpp`, `CueletCli.cpp`, `audio_service_tests.cpp`; size includes GTK model wiring.
4. **Shortcuts/drag-drop (M, P1):** implement desktop-safe local/global shortcut policy and GTK file drag/drop with copy/link choice. Risk: compositor/desktop differences.
5. **Settings/persistence (M, P0):** expose equivalent playback, library, view, shortcut and routing settings while using XDG paths; add migration/error states.
6. **Lifecycle (M, P1):** implement app actions, optional StatusNotifierItem integration, hide/reopen policy and clean audio/shortcut shutdown. Acceptance across GNOME/KDE where supported.
7. **Audio routing (L, P1):** use PipeWire first with PulseAudio fallback for physical input/output, monitor mix and endpoint health. Keep local monitoring independent from virtual output.
8. **Virtual microphone strategy (L, P2):** create/document user-space PipeWire virtual source/sink nodes or integrate existing tools; no kernel driver commitment. Acceptance: chat app can select the capture node and local output remains physical.
9. **Tests/packaging (M, P0):** add GTK interaction tests, PipeWire integration tests with safe temporary nodes, CLI coverage, Flatpak/AppImage packaging and permissions documentation.

