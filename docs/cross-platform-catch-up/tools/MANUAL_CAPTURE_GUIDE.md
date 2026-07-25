# Manual Windows screenshot capture guide

This workflow is designed for a human-operated Windows desktop and does not require Codex computer-use capability. It copies the pixels currently visible on the Cuelet window, rather than using `PrintWindow`, which can produce black WinUI content.

## Start or resume

From the repository root, run Windows PowerShell:

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\docs\cross-platform-catch-up\tools\start-manual-capture-session.ps1
```

The script launches the existing Debug executable with `-NoBuild -AllowTestDriver`, checks the executable path and a present `OK` Realtek device, then asks you to type `YES` after confirming Windows normal output is the physical speakers/headphones. It never changes the library, settings, driver, or default audio device. Enter a state number or exact state name, arrange Cuelet to that state, and press Enter when ready. Type `FINISH` at any time. Re-run the same command to resume; completed states remain in `metadata/manual-capture-session.json`.

Captures are written to `screenshots/<state-name>.png`. Names are restricted to descriptive ASCII state names and existing files are never overwritten unless the capture helper is explicitly called with `-Force`.

## State preparation

Use the required first-pass list shown by the session script. For library states, use a small deterministic demo folder containing safe sample audio and no personal filenames. For settings, scroll to the named section before capture. For `sound-context-menu`, `category-editor`, `rename-sound`, and `shortcut-capture`, open the menu/dialog and leave it visible; the screen-copy includes visible menus only when they are on-screen. For `tray-context-menu`, open the tray menu and ensure no notification or private application is behind it. For `window-maximized` and `navigation-collapsed`, change the window state before selecting the capture.

Do not include chat windows, email, notifications, personal paths, private filenames, tokens, or real-library contents. Stop and replace the demo data if any private content is visible. Do not uninstall/reinstall the virtual-audio driver.

## Direct helper use

After arranging a state manually, capture it directly:

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\docs\cross-platform-catch-up\tools\capture-cuelet-window.ps1 -Name library-populated
```

The helper finds a visible top-level Cuelet window, copies its window rectangle including title bar, writes lossless PNG, rejects duplicate names, rejects invalid/too-small rectangles, and rejects frames that are more than 99% black. It prints the absolute path, dimensions, and byte size. Use `-Force` only when intentionally replacing a known capture.

