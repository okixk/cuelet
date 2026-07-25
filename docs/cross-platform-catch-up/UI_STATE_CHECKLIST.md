# UI state checklist

States below were checked against the live Debug window. Captures use an isolated generated WAV library and the original library selection was restored afterward. “Not captured” means a specific safe limitation remains; it does not claim the source feature is absent.

| Area/state | Status | Evidence/why |
|---|---|---|
| clean launch/default window | Captured | [app-default-window.png](screenshots/app-default-window.png) |
| existing empty library | Captured | [uncategorized-view.png](screenshots/uncategorized-view.png) is the safe empty-scope equivalent; changing the real library was avoided |
| populated grid | Captured | [library-populated.png](screenshots/library-populated.png) |
| selected card | Captured | [sound-context-menu.png](screenshots/sound-context-menu.png) shows selected demo card |
| playing | Captured | [library-playing-sound.png](screenshots/library-playing-sound.png) |
| paused | Not captured | Requires a second transient playback action; no private input needed, but the current session ended after safe playback evidence |
| list view | Not captured | Safe but not reached before the session window state changed |
| hover/long title/missing metadata | Not captured | Requires controlled fixture data beyond the two minimal demo WAVs |
| favorites/recent/uncategorized/all categories | Captured | [favorites-view.png](screenshots/favorites-view.png), [recent-view.png](screenshots/recent-view.png), [uncategorized-view.png](screenshots/uncategorized-view.png), [all-categories-view.png](screenshots/all-categories-view.png) |
| category list/empty | Captured | [all-categories-view.png](screenshots/all-categories-view.png) |
| category create/editor | Captured | [category-editor.png](screenshots/category-editor.png) |
| category rename/color/icon/delete | Not captured | Would require committing or deleting category data; avoided to keep the demo fixture unchanged |
| search results/zero results | Captured | [search-results.png](screenshots/search-results.png), [search-no-results.png](screenshots/search-no-results.png) |
| keyboard result/Enter play | Not captured | Requires an additional focused-keyboard interaction; playback is evidenced separately |
| mini-player/progress | Captured | [mini-player-playing.png](screenshots/mini-player-playing.png) |
| pause/stop/volume/overlap | Not captured | Safe controls remain available, but were not separately rendered in this pass |
| sound context menu | Captured | [sound-context-menu.png](screenshots/sound-context-menu.png) |
| rename sound | Captured | [rename-sound.png](screenshots/rename-sound.png) |
| category context menu | Not captured | No existing category was modified; creating one would add fixture metadata |
| shortcut capture | Captured | [shortcut-capture.png](screenshots/shortcut-capture.png) |
| shortcut conflict/removal/settings | Not captured | Requires assigning/removing a global shortcut; avoided unnecessary global registration |
| drag accepted/rejected/category/outbound | Not captured | Requires file-manager interaction and outbound drag state |
| settings audio routing | Captured | [settings-audio-routing.png](screenshots/settings-audio-routing.png) |
| settings virtual microphone connected | Captured | [settings-virtual-microphone-connected.png](screenshots/settings-virtual-microphone-connected.png) |
| settings diagnostics expanded | Captured | [settings-driver-diagnostics-expanded.png](screenshots/settings-driver-diagnostics-expanded.png) |
| settings microphone mixing/local playback | Not captured | Settings controls were not changed because that would alter active audio configuration |
| tray/show/hide/reopen/exit | Not captured | Tray is outside the Cuelet client rectangle and no desktop Computer Use surface is available for safe tray capture |
| missing file/import/errors | Not captured | Requires intentionally invalid or missing files; avoided destructive/error fixture changes |
| narrow/wide/scroll/theme | Not captured | Maximized and collapsed states captured; theme and narrow/scroll variants remain safe follow-up states |
| navigation collapsed | Captured | [navigation-collapsed.png](screenshots/navigation-collapsed.png) |
| window maximized | Captured | [window-maximized.png](screenshots/window-maximized.png) |
