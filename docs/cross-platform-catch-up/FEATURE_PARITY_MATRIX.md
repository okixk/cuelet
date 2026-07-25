# Feature parity matrix

Statuses are based on source inspection, not visual equivalence. `Needs verification` means the implementation exists or may exist but needs a running-platform check.

| Area | Feature | Windows | macOS | Linux | Priority | Screenshot | Notes |
|---|---|---|---|---|---|---|---|
| library | scan/import supported audio | Complete | Partial | Partial | P0 | [library-populated](screenshots/library-populated.png) | Windows supports copy/link and recursive scan |
| library | grid/list, scopes, sorting | Complete | Partial | Needs verification | P0 | [library-populated](screenshots/library-populated.png), [all-categories-view](screenshots/all-categories-view.png) | macOS has library/search views; Linux UI needs run check |
| metadata | schema-v2 title/category/favorite/notes/aliases | Complete | Partial | Partial | P0 | None | Portable JSON semantics are target |
| categories | create/edit/colors/icons/delete | Complete | Partial | Needs verification | P0 | [category-editor](screenshots/category-editor.png) | macOS has category model/sidebar actions |
| search | ranked search and Enter-to-play | Complete | Partial | Needs verification | P0 | [search-results](screenshots/search-results.png), [search-no-results](screenshots/search-no-results.png) | Windows supports notes/aliases and keyboard commands |
| playback | local play/stop/overlap/volume/progress | Complete | Partial | Partial | P0 | [library-playing-sound](screenshots/library-playing-sound.png) | AVAudioPlayer exists on macOS |
| mini-player | now-playing progress controls | Complete | Complete | Needs verification | P1 | [mini-player-playing](screenshots/mini-player-playing.png) | macOS has NowPlayingMiniPlayerView |
| shortcuts | per-sound capture/conflicts/global registration | Complete | Partial | Needs verification | P1 | [shortcut-capture](screenshots/shortcut-capture.png) | macOS local keyboard service is present |
| drag and drop | import and file drag-out | Complete | Partial | Needs verification | P1 | None | Windows has explicit overlay and rules |
| lifecycle | tray/background/single instance | Complete | Different by design | Needs verification | P1 | None | macOS should use menu bar; Linux tray depends on desktop |
| settings | persistence and defaults | Complete | Partial | Partial | P0 | [settings-audio-routing](screenshots/settings-audio-routing.png) | Windows HKCU; macOS JSON settings store |
| routing | physical input/output and local monitor | Complete | Partial | Partial | P1 | [settings-audio-routing](screenshots/settings-audio-routing.png) | Platform audio APIs differ |
| virtual microphone | paired render/capture driver | Complete | Missing | Missing | P2 | [settings-virtual-microphone-connected](screenshots/settings-virtual-microphone-connected.png) | Windows driver-specific; no custom cross-platform driver promised |
| diagnostics | endpoint classification and health | Complete | Missing | Missing | P2 | [settings-driver-diagnostics-expanded](screenshots/settings-driver-diagnostics-expanded.png) | Windows WASAPI implementation |
| onboarding | create/use/missing library | Complete | Partial | Needs verification | P0 | None | Must match behavior, not widgets |
| CLI | list/play/import/show/hide commands | Complete | Needs verification | Partial | P1 | None | Inspect platform entry points before claiming parity |
| accessibility | automation names/native controls | Partial | Partial | Needs verification | P1 | None | Windows has many AutomationProperties; audit remaining controls |
| error handling | invalid paths, missing files, import/dialog errors | Complete | Partial | Partial | P0 | None | Test behavior on each platform |
| packaging | Debug, packaged Release, installer | Partial | Partial | Partial | P2 | None | Windows has project/scripts; signing/deployment remain operational work |
