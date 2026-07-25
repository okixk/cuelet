The documentation and parity analysis are useful, but the screenshot requirement is not complete.

Only one black diagnostic PNG was produced. Do not treat the visual inventory as finished.

Create a reliable manual Windows screenshot-capture workflow under:

docs/cross-platform-catch-up/tools/

The workflow must work without Codex computer-use capability.

Implement:

1. `capture-cuelet-window.ps1`
   - Capture the currently visible Cuelet window using the real on-screen pixels.
   - Prefer a screen-copy method based on the window rectangle rather than
     PrintWindow if PrintWindow produces black WinUI content.
   - Find the Cuelet top-level window automatically.
   - Capture only the Cuelet window, including title bar and visible menus.
   - Save lossless PNG files.
   - Accept a required descriptive filename or state name.
   - Refuse invalid or duplicate names unless `-Force` is supplied.
   - Verify that the image is not almost completely black or empty.
   - Print the final image dimensions, path, and file size.
   - Never capture unrelated windows or the whole desktop when the Cuelet
     window cannot be identified safely.

2. `start-manual-capture-session.ps1`
   - Launch the correct existing Windows Debug app using:
     `run-windows.ps1 -Configuration Debug -NoBuild -AllowTestDriver`
   - Confirm the launched executable path.
   - Confirm Windows normal output remains the physical Realtek device.
   - Display an ordered checklist of screenshot states.
   - Let the user select or enter the current state name.
   - Call `capture-cuelet-window.ps1`.
   - Mark the state as captured in a session JSON file.
   - Continue until the user chooses to finish.
   - Do not delete or reset the user’s real library.

3. `MANUAL_CAPTURE_GUIDE.md`
   - Explain the exact command to start.
   - Explain which Cuelet state the user should open for each screenshot.
   - Explain how to capture context menus and dialogs.
   - Explain how to avoid private content.
   - Explain how to resume an interrupted session.

Use this required first-pass capture list:

- app-default-window
- library-populated
- library-playing-sound
- favorites-view
- recent-view
- uncategorized-view
- category-selected
- category-editor
- search-results
- search-no-results
- sound-context-menu
- rename-sound
- shortcut-capture
- mini-player-playing
- settings-audio-routing
- settings-virtual-microphone-connected
- settings-driver-diagnostics-expanded
- settings-microphone-mixing
- settings-local-playback
- tray-context-menu
- navigation-collapsed
- window-maximized
- empty-category

Do not require every state in one session. Preserve progress.

After creating the tooling:

- test one capture against the currently visible Cuelet window;
- confirm the resulting PNG contains visible non-black UI pixels;
- do not claim the full screenshot inventory is complete;
- report the exact command the user should run next.

Do not modify application or driver source code for this task.