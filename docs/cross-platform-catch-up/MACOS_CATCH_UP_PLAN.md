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

## 16. Screenshot and Real-Mac Visual Validation Checklist

### Existing visual evidence

No macOS screenshots, UI captures, previews, or test-artifact images were found in the repository. The existing images below were inspected directly and must not be presented as current macOS evidence:

- Windows reference captures: `docs/cross-platform-catch-up/screenshots/01-launch-and-onboarding/launch-debug.png`, `docs/cross-platform-catch-up/screenshots/app-default-window.png`, `docs/cross-platform-catch-up/screenshots/app-default-window-foreground.png`, `docs/cross-platform-catch-up/screenshots/debug-current.png`, `docs/cross-platform-catch-up/screenshots/library-populated.png`, `docs/cross-platform-catch-up/screenshots/favorites-view.png`, `docs/cross-platform-catch-up/screenshots/recent-view.png`, `docs/cross-platform-catch-up/screenshots/all-categories-view.png`, `docs/cross-platform-catch-up/screenshots/uncategorized-view.png`, `docs/cross-platform-catch-up/screenshots/search-results.png`, `docs/cross-platform-catch-up/screenshots/search-no-results.png`, `docs/cross-platform-catch-up/screenshots/library-playing-sound.png`, `docs/cross-platform-catch-up/screenshots/mini-player-playing.png`, `docs/cross-platform-catch-up/screenshots/sound-context-menu.png`, `docs/cross-platform-catch-up/screenshots/category-editor.png`, `docs/cross-platform-catch-up/screenshots/rename-sound.png`, `docs/cross-platform-catch-up/screenshots/shortcut-capture.png`, `docs/cross-platform-catch-up/screenshots/settings-audio-routing.png`, `docs/cross-platform-catch-up/screenshots/settings-virtual-microphone-connected.png`, `docs/cross-platform-catch-up/screenshots/settings-driver-diagnostics-expanded.png`, `docs/cross-platform-catch-up/screenshots/navigation-collapsed.png`, and `docs/cross-platform-catch-up/screenshots/window-maximized.png`.
- Linux GNOME/Wayland captures: `docs/cross-platform-catch-up/linux-screenshots/library-populated.png`, `docs/cross-platform-catch-up/linux-screenshots/category.png`, `docs/cross-platform-catch-up/linux-screenshots/category-editor.png`, `docs/cross-platform-catch-up/linux-screenshots/search-results.png`, `docs/cross-platform-catch-up/linux-screenshots/search-no-results.png`, `docs/cross-platform-catch-up/linux-screenshots/playback.png`, `docs/cross-platform-catch-up/linux-screenshots/settings.png`, `docs/cross-platform-catch-up/linux-screenshots/audio-routing.png`, `docs/cross-platform-catch-up/linux-screenshots/empty-state.png`, `docs/cross-platform-catch-up/linux-screenshots/collapsed.png`, and `docs/cross-platform-catch-up/linux-screenshots/maximized.png`.

The Windows images visibly demonstrate a generated demo library, grid/scopes, search, playback, mini-player, category and rename dialogs, shortcut capture, settings, navigation collapse, maximized layout, and a sound context menu. They are useful interaction-state references, but their WinUI chrome and layout are not evidence of SwiftUI/AppKit behavior; menu/dialog images prove visibility only, not that the action succeeded. The Linux images visibly demonstrate the current GTK implementation's populated, categorized, missing, empty, search, playback, settings, routing, collapsed, and maximized states. They are not macOS evidence. The Linux index explicitly records that no context-menu PNG is claimed. No image in this repository verifies current macOS rendering, macOS accessibility, macOS privacy prompts, or macOS audio-device behavior. The existing Windows/Linux captures also use different fixture names and platform controls, so they are incomplete as a macOS acceptance set rather than stale macOS captures.

### Future real-Mac capture checklist

On real macOS hardware, launch the actual Finder-launchable `Cuelet.app` from an isolated temporary configuration and capture the following states. Use the exact before/after pairs marked below; a visible control without a changed result is not an interaction pass.

1. Initial launch and onboarding.
2. Empty library.
3. Populated grid view.
4. Populated list view.
5. Narrow, default-size, wide, and maximized windows.
6. Long filenames and Unicode filenames.
7. Managed, linked, and missing sounds.
8. Favorite state and multiple categories with colors/icons.
9. Search results and category filtering.
10. Context menu open, then the visible result of favorite, category assignment, rename, remove, and safe managed deletion.
11. Import file picker, copy-versus-link choice, duplicate/filename-collision warning, and imported item after restart.
12. Rename dialog followed by renamed sound; category-assignment dialog followed by updated category.
13. Remove-from-library confirmation followed by preserved file and metadata state; delete-managed-file confirmation followed by removed file and metadata.
14. Missing-file recovery/relink dialog followed by restored playable item.
15. Mini-player playing, paused, resumed, stopped, and multiple simultaneous sounds if supported.
16. Settings, global-shortcut configuration, shortcut conflict/permission state, audio-output selection, and each changed result after restart.
17. Virtual-microphone unavailable state; installed-and-ready state if implemented; physical-microphone mixing indicator.
18. Error and recovery dialogs.
19. VoiceOver-focused controls where visually observable, increased text size, increased contrast, and reduced-motion behavior where a still image demonstrates it.
20. Final polished default grid and final polished default list.

Every state involving an action requires a capture before the action and after the result: menu-open → changed item, import dialog → item after restart, rename dialog → renamed item, missing file → relinked item, shortcut registration → assigned shortcut, virtual microphone disabled → enabled/visible, managed deletion confirmation → file/metadata removed, and remove-from-library → source file preserved. Capture only the Cuelet window or a tightly bounded dialog/menu surface; preserve enough title/sidebar/context to establish the state.

### Capture and fixture requirements

- Write captures outside the repository, for example `/tmp/cuelet-macos-catch-up-<timestamp>/screenshots/`, and never add copied or generated PNGs to Git.
- Use deterministic temporary media with neutral names such as `Fixture Bell.wav`, `Fixture Unicode é.wav`, `Fixture Long Filename ...wav`, `Fixture Linked.wav`, and `Fixture Missing.wav`; do not expose usernames, home paths, private filenames, notifications, desktop content, microphone names, or unrelated applications.
- Record for every image: exact filename, macOS version, architecture, display scale, window size, application build/commit, whether the state was merely visible or interactively verified, and the isolated fixture/configuration used.
- Use names such as `01-launch.png`, `02-empty-library.png`, `03-grid-populated-before.png`, `03-grid-populated-after.png`, `10-context-menu-before.png`, and `10-context-menu-after-favorite.png`; keep names descriptive ASCII and never overwrite an existing capture unintentionally.
- Apple Silicon testing is required for all launch, layout, import, playback, settings, shortcut, privacy, accessibility, and audio-routing states. Intel testing is required only if Intel remains a supported deployment target; if it does, repeat the same compatibility-sensitive states, especially audio devices, global shortcuts, microphone permissions, and virtual-audio integration.
- A real microphone is required for permission, input-metering, and physical-microphone-mixing states. An external audio device is required for output-selection and unavailable-device recovery. An installed, supported virtual-audio component such as BlackHole is required for virtual-microphone-ready and receiving-app validation; Cuelet must not claim a built-in virtual device if none is installed.
- Screenshots are visual evidence only when captured from the real running macOS application. Pair them with logs or test records for behavior that cannot be established visually, and never substitute source inspection, Linux, Windows, mock-up, or generated UI for a real-Mac capture.
