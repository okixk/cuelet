# Feature parity matrix

Statuses combine source inspection with the validation recorded in this
folder. `Needs verification` means the implementation exists or may exist but
still needs a running-platform check.

| Area | Feature | Windows | macOS | Linux | Priority | Screenshot | Notes |
|---|---|---|---|---|---|---|---|
| library | scan/import supported audio | Complete | Partial | Complete | P0 | [Windows](screenshots/library-populated.png), [Linux](linux-screenshots/library-populated.png) | Linux supports recursive scan plus copy/link file, folder, and drop import; symlinks are rejected |
| library | grid/list, scopes, sorting | Complete | Partial | Complete | P0 | [Windows](screenshots/all-categories-view.png), [Linux](linux-screenshots/category.png) | Linux grid/list, scopes, sorting, missing and empty states ran on Wayland |
| metadata | schema-v2 title/category/favorite/notes/aliases | Complete | Partial | Complete | P0 | [Linux populated](linux-screenshots/library-populated.png) | Linux adds linked-source and duration-fingerprint persistence with tolerant atomic I/O |
| categories | create/edit/colors/icons/delete | Complete | Partial | Complete | P0 | [Windows](screenshots/category-editor.png), [Linux](linux-screenshots/category-editor.png) | GTK uses native dialogs and a stable Linux icon mapping |
| search | ranked search and Enter-to-play | Complete | Partial | Complete | P0 | [Linux results](linux-screenshots/search-results.png), [Linux no results](linux-screenshots/search-no-results.png) | Search, aliases/notes/categories, focus, Enter, and Escape are implemented |
| playback | local play/pause/stop/overlap/volume/progress | Complete | Partial | Complete | P0 | [Windows](screenshots/library-playing-sound.png), [Linux](linux-screenshots/playback.png) | Linux uses GStreamer with explicit state and cleanup |
| mini-player | now-playing progress controls | Complete | Complete | Complete | P1 | [Windows](screenshots/mini-player-playing.png), [Linux](linux-screenshots/playback.png) | Linux mini-player includes progress, pause/resume, stop, volume and Stop All |
| shortcuts | per-sound capture/conflicts/global registration | Complete | Partial | Partial | P1 | [Windows](screenshots/shortcut-capture.png) | Linux local capture/conflicts are complete; GNOME custom-command integration replaces unsafe Wayland grabs |
| drag and drop | import and file drag-out | Complete | Partial | Partial | P1 | None | Linux native drag-in is complete; file drag-out remains missing |
| lifecycle | tray/background/single instance | Complete | Different by design | Partial | P1 | None | Linux forwards show/hide/play/stop/exit and notifies hidden playback; stock-GNOME tray is not promised |
| settings | persistence and defaults | Complete | Partial | Complete | P0 | [Windows](screenshots/settings-audio-routing.png), [Linux](linux-screenshots/settings.png) | Linux validates XDG JSON values, writes private settings atomically, and persists library/view/playback/import/output/link-approval settings |
| routing | physical input/output and local monitor | Complete | Partial | Partial | P1 | [Linux audio](linux-screenshots/audio-routing.png) | Linux can target PipeWire/PulseAudio output; input enumeration, mic mix and virtual-route local monitor remain |
| virtual microphone | paired render/capture endpoint | Complete | Missing | Partial | P2 | [Windows](screenshots/settings-virtual-microphone-connected.png), [Linux](linux-screenshots/audio-routing.png) | Linux creates temporary user-session PipeWire sink/source nodes; no kernel driver or receiving-app claim |
| diagnostics | endpoint classification and health | Complete | Missing | Partial | P2 | [Linux routing](linux-screenshots/audio-routing.png) | Linux reports tool/session/process state and scoped cleanup; full endpoint enumeration/classification remains |
| onboarding | create/use/missing library | Complete | Partial | Complete | P0 | [Linux empty](linux-screenshots/empty-state.png) | Native empty, missing-folder, no-results, and populated states were exercised |
| CLI | list/play/import/show/hide commands | Complete | Needs verification | Partial | P1 | None | Linux lists/searchable metadata and forwards library/play/stop/show/hide/rescan/exit; CLI import is absent |
| accessibility | automation names/native controls | Partial | Partial | Partial | P1 | [Linux collapsed](linux-screenshots/collapsed.png) | Linux cards/rows expose labels, descriptions and selected state; a formal AT-SPI audit remains |
| error handling | invalid paths, missing files, import/dialog errors | Complete | Partial | Complete | P0 | [Linux missing](linux-screenshots/library-populated.png) | Focused tests cover malformed metadata/settings, missing media, collisions, traversal, symlink rejection, unapproved links, and lifecycle failure |
| packaging | Debug, packaged Release, installer | Partial | Partial | Partial | P2 | None | Windows has project/scripts; signing/deployment remain operational work |
