# macOS catch-up plan

Last execution review: 2026-08-01 on macOS 26.6, Apple M1, arm64. Claims below were checked against the current Swift source and then classified by build/test/runtime evidence. Detailed commands, screenshot evidence, and limitations are in [MACOS_VALIDATION.md](MACOS_VALIDATION.md). The full virtual-audio boundary is in [MACOS_VIRTUAL_AUDIO_DESIGN.md](MACOS_VIRTUAL_AUDIO_DESIGN.md).

## Current implementation status

1. **Durable library/data model (P0 — implemented and tested):** schema-v2 native Swift metadata now records stable UUIDs, managed/linked storage, relative managed paths, external paths/bookmarks, display/original names, missing/favorite/category/notes/aliases/shortcut data, cached duration, timestamps, and native filesystem identity. Legacy settings and v1 metadata migrate with safety copies and idempotence tests.
2. **Persistence safety (P0 — implemented and tested):** settings and library metadata use same-filesystem temporary files, POSIX atomic rename, mode `0600`, recovery copies, explicit decode errors, and fail-closed startup behavior. A corrupt file no longer silently resets user data to empty defaults.
3. **Import/filesystem semantics (P0 — implemented, tested, and runtime exercised):** Copy and Link are explicit native choices. Managed copies use collision-safe names and roll back if metadata cannot be committed. Linked bookmarks resolve across launches/moves. Remove is metadata-only; managed deletion is separate, confirmed, containment/identity checked, staged, and unavailable for linked/missing items. Missing entries persist and can be located/relinked.
4. **Playback state (P1 — implemented and runtime exercised):** an injectable backend retains one `AVAudioPlayer` per sound and supports pause/resume, simultaneous players, rapid replay replacement, progress while paused, stop/stop-all, missing/damaged/route-failure refusal, security-scope lifetime, EOF cleanup, termination cleanup, and stable-UID single-destination routing. Static per-player headroom prevents the sum of simultaneous players from exceeding Cuelet's global gain.
5. **Global shortcuts (P1 — implemented and runtime exercised):** Carbon registrations use stable sound UUIDs, survive rename/reorder/filter/restart, report conflicts/failures, unregister on shutdown, and are temporarily suspended while the recorder captures a key. F13 was exercised with Finder, Terminal, and Safari focused without Accessibility permission.
6. **Native UI and action policy (P1/P2 — substantially improved):** grid/list items identify managed, linked, and missing states; context actions are policy-driven; destructive confirmations are distinct; mini-player pause/resume is exposed; search includes notes/aliases; toolbar/state feedback loops were guarded; Settings states routing limits truthfully. Pointer workflows were exercised in the real release bundle.
7. **Accessibility (P2 — source/test improvement, runtime coverage incomplete):** important cards, missing states, favorite/category state, playback controls, toolbar mode, and output-routing states have explicit accessibility labels/values and color-independent text/icon states. VoiceOver focus/announcement behavior was checked for the output picker only; the application-wide pass, contrast, text-size, and reduced-motion checks remain open.
8. **Performance (P2 — measured, no optimization required yet):** generated scanner benchmarks cover 250 and 1,000 files; the real release app also rendered a 1,000-file library. Measurements are recorded in the validation document. Instruments-level interaction profiling is still open.
9. **Packaging (P2/P3 — local only):** SwiftPM Debug/Release, the app-bundle script, and the independent Audio Server Driver Plug-in build are reproducible on arm64. Both bundles are ad-hoc signed and structurally verify. Production Developer ID signing, notarization, a polished installer, and an updater are not configured.
10. **Selected output and Cuelet virtual audio (P1 — source, tests, and pre-install validation complete):** Cuelet supports System Output or one explicit Core Audio output by stable UID without changing the system default. An existing virtual output was selected, restored, and received a measured signal from the real Release app. Cuelet now also owns an arm64 Audio Server Driver Plug-in with one stereo output-to-input loopback device, stable UIDs, a bounded atomic ring, tests, exact-path install/uninstall scripts, and app readiness states. It has not been installed or loaded yet, so its external recording path remains a post-restart validation item. Physical-microphone mixing and combined speaker output remain absent.

## Next execution order

1. After explicit approval, install only the verified Cuelet driver bundle, restart manually, then validate Core Audio visibility, Cuelet output injection, external-app input capture, signal integrity, selection churn, and exact cleanup.
2. Perform a controlled physical USB/headphone disconnect/reconnect pass to validate Core Audio loss notifications, both fallback policies, and exact-UID restoration without touching the primary system output.
3. Run the application-wide VoiceOver, Full Keyboard Access, increased text/contrast, and reduced-motion validation; repair only reproduced gaps.
4. Add drag-in/drag-out, window restoration/full-screen validation, and an XCUITest target once a checked-in Xcode project/scheme exists.
5. Design a synchronized multi-destination backend only if speaker-plus-virtual output becomes a confirmed product requirement; the current backend intentionally targets one output.
6. Establish Developer ID/Hardened Runtime/notarization only after a distribution decision and signing environment exist.

## 16. Screenshot and Real-Mac Visual Validation Checklist

### Existing visual evidence

The 2026-08-01 validation produced real macOS screenshots outside Git at `/tmp/cuelet-macos-validation-20260731-acLoOg/screenshots/`. Its `manifest.md` records the OS, architecture, display scale, window size, fixture, action, proof, and interactive-verification status for each capture. It covers launch/empty/populated grid and list, window widths, Unicode/long names, managed/linked/missing states, favorites/categories/search, context actions, copy/link/duplicate import, rename, removal/deletion, relink, playback/pause/resume/multiple playback, Settings, global shortcut assignment/conflict, truthful routing/virtual-device states, and a 1,000-file grid.

The output-routing pass added 22 real-app PNGs at `/tmp/cuelet-macos-routing-20260801-BmrLkx/screenshots/`; its `manifest.md` records System Output, explicit-UID selection/confirmation, simultaneous and paused playback, installed virtual-device selection/restart/measured delivery, fallback policy, unresolved saved UID, active fallback, and VoiceOver-focused output-selector states. Signal delivery is paired with Core Audio/AVFoundation receiver measurements because a settings screenshot alone cannot prove destination audio.

The Cuelet-owned driver pre-install pass added 14 privacy-scoped PNGs at `/tmp/cuelet-macos-driver-20260801-120333/screenshots/`. Its `manifest.md` covers the new prepared/not-installed UI, driver build and bundle checks, no-root install preflight, technical/install/uninstall details, sanitizer correction, and final checklist. It explicitly omits the Restart required capture until an approved install actually occurs.

Screenshots stay private and outside the repository. They are paired with isolated filesystem/JSON checks for actions that an image cannot prove, including import persistence, linked-file preservation, managed deletion, relinked path, and shortcut `lastPlayedAt` updates. Windows and Linux captures remain behavior references only and are not treated as macOS evidence.

### Remaining real-Mac capture checklist

The following requested states were not completed in this pass and must not be inferred from existing images:

1. Maximized/full-screen behavior and window restoration.
2. Drag-in and drag-out.
3. A filename-collision result in the real UI (automated coverage exists).
4. Corrupt-metadata recovery and migration banners in a real app session (automated coverage exists).
5. Controlled physical USB/headphone removal and reconnection; invalid saved UID and destination audio now have runtime evidence, but no hardware was disconnected.
6. A second third-party virtual-device/receiving-application combination; the installed virtual transport was verified with an AVFoundation receiver only.
7. Physical-microphone permission, visible active indicator, and mixing behavior only if mixing is implemented.
8. Application-wide VoiceOver/Full Keyboard Access, increased text size, increased contrast, and reduced motion; the output selector alone has VoiceOver evidence.
9. Intel and older supported-macOS compatibility if those environments remain in the support matrix.

Every remaining state involving an action requires before/action/result evidence. Capture only the Cuelet window or a tightly bounded dialog/menu surface; preserve enough title/sidebar/context to establish the state.

### Capture and fixture requirements

- Write captures outside the repository, for example `/tmp/cuelet-macos-catch-up-<timestamp>/screenshots/`, and never add copied or generated PNGs to Git.
- Use deterministic temporary media with neutral names such as `Fixture Bell.wav`, `Fixture Unicode é.wav`, `Fixture Long Filename ...wav`, `Fixture Linked.wav`, and `Fixture Missing.wav`; do not expose usernames, home paths, private filenames, notifications, desktop content, microphone names, or unrelated applications.
- Record for every image: exact filename, macOS version, architecture, display scale, window size, application build/commit, whether the state was merely visible or interactively verified, and the isolated fixture/configuration used.
- Use names such as `01-launch.png`, `02-empty-library.png`, `03-grid-populated-before.png`, `03-grid-populated-after.png`, `10-context-menu-before.png`, and `10-context-menu-after-favorite.png`; keep names descriptive ASCII and never overwrite an existing capture unintentionally.
- Apple Silicon testing is required for all launch, layout, import, playback, settings, shortcut, privacy, accessibility, and audio-routing states. Intel testing is required only if Intel remains a supported deployment target; if it does, repeat the same compatibility-sensitive states, especially audio devices, global shortcuts, microphone permissions, and virtual-audio integration.
- A real microphone is required for permission, input-metering, and physical-microphone-mixing states. A disposable external audio device is required for physical output-removal recovery. Cuelet's own virtual transport must be described as prepared, not Ready, until the exact installed bundle and live input/output UID are both present.
- Screenshots are visual evidence only when captured from the real running macOS application. Pair them with logs or test records for behavior that cannot be established visually, and never substitute source inspection, Linux, Windows, mock-up, or generated UI for a real-Mac capture.
