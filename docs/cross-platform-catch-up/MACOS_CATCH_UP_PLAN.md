# macOS catch-up plan

1. **Shared behavior/data model (M, P0):** compare `SoundClip`, `SoundCategory`, metadata precedence, search ranking and settings against Windows; add cross-platform fixtures and acceptance tests. Targets: `core/cuelet-core`, `apps/macos/Cuelet/Models`, `Services/LibraryService.swift`. Depends on schema decisions; risk is model drift.
2. **Navigation/library UI (M, P0):** match onboarding, scopes, grid/list, sort, empty/missing states. Targets `Views/SoundLibraryView.swift`, `SidebarView.swift`, `Components`. Acceptance: same scope and sort results for fixture library; no Windows-specific widgets.
3. **Categories/search/playback (L, P0):** finish category CRUD visuals, ranked search and Enter play, mini-player progress/pause/stop/volume. Targets `SoundLibraryView.swift`, `SoundContextMenu.swift`, `PlaybackService.swift`. Screenshot refs pending capture.
4. **Shortcuts/drag-drop (M, P1):** map Windows conflict rules to `ShortcutCaptureService.swift` and local/global macOS policy; implement copy/link and category drop acceptance. Risk: permissions and Finder drag semantics.
5. **Settings/persistence (M, P0):** align defaults and sections with Windows while retaining `SettingsStore.swift`; add routing/monitoring settings and migration tests.
6. **Lifecycle (S, P1):** menu-bar/background behavior, reopen/show/hide, shortcut cleanup, launch-at-login policy using LoginItems. Acceptance: no orphan audio players or registrations.
7. **Audio routing (L, P1):** use AVAudioSession/Core Audio device enumeration for physical I/O and monitoring; distinguish local output from chat output. Acceptance: selected devices survive restart and unavailable devices show a warning.
8. **Virtual microphone strategy (L, P2):** integrate a supported existing virtual device such as BlackHole where available; expose capability/disconnected states, never claim a built-in driver. Risk: installation, permissions, sample rates.
9. **Tests/packaging (M, P0):** extend `CueletTests`, fixture screenshots on macOS, signed/notarized packaging and an install/uninstall-safe routing test plan.

