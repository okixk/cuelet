# Current Windows UI

## Evidence and scope

The UI is a native WinUI 3/C++/WinRT window with Mica, an integrated title bar, and `NavigationView`. The structure below comes from `MainWindow.xaml` and behavior from `MainWindow.xaml.cpp`; no visual claim is made for states that were not rendered successfully. The default Debug executable launched successfully at the path recorded in `metadata/repository-state.txt`.

Rendered evidence from the isolated demo-library session: [default window](screenshots/app-default-window.png), [populated library](screenshots/library-populated.png), [search results](screenshots/search-results.png), [no results](screenshots/search-no-results.png), [playing](screenshots/library-playing-sound.png), [mini-player](screenshots/mini-player-playing.png), [sound menu](screenshots/sound-context-menu.png), [category editor](screenshots/category-editor.png), [rename](screenshots/rename-sound.png), [shortcut capture](screenshots/shortcut-capture.png), [routing settings](screenshots/settings-audio-routing.png), [diagnostics](screenshots/settings-driver-diagnostics-expanded.png), [collapsed navigation](screenshots/navigation-collapsed.png), and [maximized window](screenshots/window-maximized.png).

## Launch and onboarding

Purpose: choose or create a sound library. The onboarding page contains “Welcome to Cuelet”, explanatory text, “Create a New Library”, and “Use an Existing Library”. If a remembered folder is missing, it exposes locate/create/choose-another actions and a missing-path message. A clean first-run visual capture is blocked.

## Navigation and library

The pane contains Library, Favorites, Recent, All Categories, Uncategorized, a Categories header, and New category. The pane can collapse and uses automatic display mode. The library header shows title and path plus Import, Rescan, and Stop all. Search is an AutoSuggestBox covering sounds, notes, and aliases. Sorting supports name ascending/descending, recently added/played, shortest/longest, and category. Grid and list toggles switch the two presentations.

The collection supports extended selection, favorite state, recent state, category scopes, missing files, drag/drop, empty messages, and a drop overlay. Cards/list rows expose play/stop, favorite, drag, duration/progress and context actions. The now-playing bar is hidden until playback and provides progress, pause/resume, stop and volume-related controls.

## Categories and editing

Categories are represented in the navigation pane and can be created, renamed, edited, assigned, or deleted. Source behavior includes category names, icons, colors, and context menus. Category deletion and metadata editing use ContentDialog. Exact visual layouts and confirmation wording require an interactive capture.

## Settings

The Settings page is a scrollable native page. Verified sections include General, Library, Keyboard, About, and the Audio routing/virtual microphone controls added in the working tree. Controls include remembered library selection, recursive scan, file extensions, grid/list default, sorting, simultaneous playback, volume, local monitoring, physical input/output selection, microphone mixing, virtual endpoint status, diagnostics, test, install/refresh/uninstall actions, shortcut registration and clearing. The app uses HKCU settings in Windows builds and `.cuelet-metadata.json` in the selected library.

## Menus, shortcuts, drag/drop, lifecycle

Sound context menus provide play/stop, favorite, category assignment, new category, shortcut capture/change/clear, rename, locate/reveal, and remove. Category menus provide edit/remove. Enter plays the focused selection or best search result; Escape clears search then selection; Ctrl+A selects visible sounds; Ctrl+F focuses search. Per-sound shortcuts are global and remain registered while hidden/trayed. The app supports single-instance command forwarding, tray/background operation, show/hide/exit commands, and file drag-out plus import drag-in.

## Known states not visually evidenced

List view, paused playback, tray menu, drop overlay, import/error states, category rename/delete, shortcut conflict/removal, and dark/light variants remain pending. They require additional safe setup or transient actions and are called out in the checklist; source presence is not treated as visual proof.
