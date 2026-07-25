# Screenshot index

The Debug window was brought to the foreground and captured by copying its on-screen rectangle. The inventory uses an isolated generated WAV library; the original library selection was restored after capture. Each listed PNG was checked for visible non-black pixels and visible Cuelet chrome/content.

| Filename | State | Reproduction | Feature/source | Safe automation | macOS/Linux equivalent |
|---|---|---|---|---|---|
| [`app-default-window.png`](screenshots/app-default-window.png) | default populated Debug window | Launch Debug, foreground Cuelet | `MainWindow.xaml` | Captured and pixel-validated | Needs platform capture |
| [`app-default-window-foreground.png`](screenshots/app-default-window-foreground.png) | duplicate foreground validation frame | Foreground Cuelet before capture | `MainWindow.xaml` | Captured and pixel-validated | Needs platform capture |
| [`debug-current.png`](screenshots/debug-current.png) | isolated demo validation frame | Current demo-library window | `MainWindow.xaml` | Captured and pixel-validated | Needs platform capture |
| [`library-populated.png`](screenshots/library-populated.png) | isolated demo library grid | Use generated demo WAV library | library grid/import | Captured and pixel-validated | Needs platform capture |
| [`favorites-view.png`](screenshots/favorites-view.png) | Favorites empty state | Select Favorites | favorites scope | Captured and pixel-validated | Needs platform capture |
| [`recent-view.png`](screenshots/recent-view.png) | Recent empty state | Select Recent | recent scope | Captured and pixel-validated | Needs platform capture |
| [`all-categories-view.png`](screenshots/all-categories-view.png) | All Categories scope | Select All Categories | category navigation | Captured and pixel-validated | Needs platform capture |
| [`uncategorized-view.png`](screenshots/uncategorized-view.png) | Uncategorized scope | Select Uncategorized | category navigation | Captured and pixel-validated | Needs platform capture |
| [`search-results.png`](screenshots/search-results.png) | search results | Enter `demo` | SearchBox/filtering | Captured and pixel-validated | Needs platform capture |
| [`search-no-results.png`](screenshots/search-no-results.png) | zero search results | Enter `no-such-sound` | SearchBox/empty state | Captured and pixel-validated | Needs platform capture |
| [`library-playing-sound.png`](screenshots/library-playing-sound.png) | playing sound | Invoke Play on demo-pulse | playback | Captured and pixel-validated | Needs platform capture |
| [`mini-player-playing.png`](screenshots/mini-player-playing.png) | mini-player while playing | Start demo-pulse | now-playing bar | Captured and pixel-validated | Needs platform capture |
| [`sound-context-menu.png`](screenshots/sound-context-menu.png) | sound context menu | Right-click demo-pulse; keep menu open | sound menu | Captured and pixel-validated | Needs platform capture |
| [`category-editor.png`](screenshots/category-editor.png) | New Category editor | Select New category… | category dialog | Captured and pixel-validated | Needs platform capture |
| [`rename-sound.png`](screenshots/rename-sound.png) | Rename dialog | Sound menu → Rename… | metadata editing | Captured and pixel-validated | Needs platform capture |
| [`shortcut-capture.png`](screenshots/shortcut-capture.png) | shortcut capture dialog | Sound menu → Change Shortcut… | shortcuts | Captured and pixel-validated | Needs platform capture |
| [`settings-audio-routing.png`](screenshots/settings-audio-routing.png) | Audio routing settings | Select Settings | routing/settings | Captured and pixel-validated | Needs platform capture |
| [`settings-virtual-microphone-connected.png`](screenshots/settings-virtual-microphone-connected.png) | virtual microphone status | Settings → Advanced audio details | virtual microphone | Captured and pixel-validated | Needs platform capture |
| [`settings-driver-diagnostics-expanded.png`](screenshots/settings-driver-diagnostics-expanded.png) | expanded driver diagnostics | Settings → View Diagnostic Details | diagnostics | Captured and pixel-validated | Needs platform capture |
| [`navigation-collapsed.png`](screenshots/navigation-collapsed.png) | collapsed navigation | Invoke Close Navigation | navigation | Captured and pixel-validated | Needs platform capture |
| [`window-maximized.png`](screenshots/window-maximized.png) | maximized window | Invoke Maximize | window state | Captured and pixel-validated | Needs platform capture |

Use `capture-window.ps1` only after an interactive session verifies that the compositor produces a non-black image. A future capture session should use a deterministic demo folder and restore the prior library/settings.

The reliable replacement workflow remains [`tools/MANUAL_CAPTURE_GUIDE.md`](tools/MANUAL_CAPTURE_GUIDE.md). The isolated demo captures above avoid real-library filenames. The remaining checklist states are documented as not captured where they would require destructive configuration changes, private content, or additional transient interaction not safely reproducible from the current Debug state.
