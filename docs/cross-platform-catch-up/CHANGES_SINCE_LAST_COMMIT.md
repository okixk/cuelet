# Changes since last commit

## Uncommitted changes compared with HEAD

The working tree contains a large Windows catch-up implementation. The principal user-visible changes are:

| Change | Affected files | Parity impact |
|---|---|---|
| Native library UI: search, scopes, sorting, grid/list, import copy/link, drag/drop, rescan, missing/empty states | `apps/windows/Cuelet.WinUI/MainWindow.xaml`, `MainWindow.xaml.cpp`, shared-core search/types files | Windows is ahead of macOS/Linux in native integration; shared semantics can be reused |
| Playback and now-playing state: MediaPlayer, duration/progress, overlap mode, Stop all, recent timestamps | `MainWindow.xaml.cpp`, `SoundTypes.*`, `SoundSearch.*` | macOS has AVAudioPlayer but needs UI/state alignment; Linux needs comparison |
| Metadata/category/shortcut editing and schema-v2 persistence | `MainWindow.xaml.cpp`, `WindowsMetadataStore.cpp`, core metadata/types | macOS has analogous models; Linux parity needs verification |
| Tray, single-instance forwarding, global shortcuts, settings/about | `App.xaml.*`, `MainWindow.*`, `WindowsHotkeyModel.cpp`, manifest/project | Native lifecycle parity remains platform-specific |
| Virtual microphone workflow, diagnostics, WASAPI routing models, installer/flow-test projects | `WindowsAudioRoutingModel.*`, `WindowsVirtualAudioModel.*`, `WindowsWorkflowModel.*`, `WindowsLifecycleModel.*`, `WindowsDiagnostics.*`, virtual-audio projects/scripts | Windows-only driver work; do not treat as shared-core parity |
| Core regression coverage and project wiring | `Cuelet.Core.Tests/*`, `.vcxproj`, solution | Provides reusable behavioral test cases |

No screenshot can be associated with these changes in this session because the available native capture returned a black frame. The source references above are evidence of implementation, not visual evidence.

## Changes introduced by the latest commit compared with HEAD^

HEAD is `abf3e888f8e0d60f06dbad91ad33d79f5da48602`; its parent is `2feb2d613dd293d94ddfdba9656e4209526b47f7`. The latest commit added the initial shared core: library scanning, metadata store, sound types/search, and related Qt-side core files (`core/*`, `src/core/*`), plus `.gitignore` and README updates. This is foundational behavior rather than the current native Windows UI delta. The exact raw name/status and stats are in `metadata/repository-state.txt`.

