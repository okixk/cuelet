# Cuelet cross-platform catch-up snapshot

This snapshot describes the Windows WinUI 3 application at working-tree HEAD and compares it with the macOS SwiftUI/AppKit and Linux GTK/libadwaita implementations. It is source-verified and launch-verified, but not a complete visual capture: this environment has no interactive computer-use channel and the native `gdigrab` capture of the launched window was black. Therefore visual states are marked `Blocked by missing computer-use capability` in the checklist rather than invented. The one retained PNG is a diagnostic record of that failed render, not a UI screenshot.

The Windows Debug executable was launched with:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .\apps\windows\scripts\run-windows.ps1 -Configuration Debug -NoBuild -AllowTestDriver
```

The executable path was `apps/windows/x64/Debug/Cuelet.WinUI/Cuelet.exe`. The helper [`capture-window.ps1`](capture-window.ps1) is intentionally limited to deterministic full-window PNG capture for a future interactive/manual run. It does not alter settings, libraries, drivers, or application code.

The reliable manual workflow is now under [`tools/`](tools/MANUAL_CAPTURE_GUIDE.md). Start it with `powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\docs\cross-platform-catch-up\tools\start-manual-capture-session.ps1`. It copies on-screen pixels, validates non-black output, refuses duplicate names, and resumes from `metadata/manual-capture-session.json`.

Read next: [CURRENT_WINDOWS_UI.md](CURRENT_WINDOWS_UI.md), [FEATURE_PARITY_MATRIX.md](FEATURE_PARITY_MATRIX.md), and [UI_STATE_CHECKLIST.md](UI_STATE_CHECKLIST.md).
