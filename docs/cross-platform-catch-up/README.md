# Cuelet cross-platform catch-up snapshot

This snapshot describes the Windows WinUI 3 application and compares it with
the macOS SwiftUI/AppKit and Linux GTK/libadwaita implementations. Windows and
Linux both have source, build, runtime, and safe screenshot evidence; the
platform-specific screenshot indexes keep those images separate. Unsupported
or unavailable states remain explicitly marked rather than inferred from
source presence.

The Windows Debug executable was launched with:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .\apps\windows\scripts\run-windows.ps1 -Configuration Debug -NoBuild -AllowTestDriver
```

The executable path was `apps/windows/x64/Debug/Cuelet.WinUI/Cuelet.exe`. The helper [`capture-window.ps1`](capture-window.ps1) is intentionally limited to deterministic full-window PNG capture for a future interactive/manual run. It does not alter settings, libraries, drivers, or application code.

The reliable manual workflow is now under [`tools/`](tools/MANUAL_CAPTURE_GUIDE.md). Start it with `powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\docs\cross-platform-catch-up\tools\start-manual-capture-session.ps1`. It copies on-screen pixels, validates non-black output, refuses duplicate names, and resumes from `metadata/manual-capture-session.json`.

Read next: [CURRENT_WINDOWS_UI.md](CURRENT_WINDOWS_UI.md),
[FEATURE_PARITY_MATRIX.md](FEATURE_PARITY_MATRIX.md),
[LINUX_CATCH_UP_PLAN.md](LINUX_CATCH_UP_PLAN.md),
[LINUX_VALIDATION.md](LINUX_VALIDATION.md),
[SCREENSHOT_INDEX.md](SCREENSHOT_INDEX.md), and
[UI_STATE_CHECKLIST.md](UI_STATE_CHECKLIST.md).
