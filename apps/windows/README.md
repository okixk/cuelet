# Cuelet for Windows

Cuelet for Windows is a native WinUI 3 and C++/WinRT application. Debug builds run unpackaged for a quick edit/build/run loop; Release builds use a single-project packaged MSIX configuration.

## Implemented phases

1. **Foundation** — Windows App SDK 2.2, C++/WinRT, XAML, Mica, integrated title bar, `NavigationView`, build/run/package scripts, and the shared-core MSVC static library.
2. **Library** — startup library onboarding, persisted folder selection, recursive scanning through `cuelet-core`, search, favorites/recent/category scopes, sorting, grid/list views, copy/link import, interoperable file drag-and-drop, rescan, missing-file handling, and useful empty/error states.
3. **Playback** — native `MediaPlayer` playback, overlapping or single-sound mode, live volume changes, duration discovery, playback progress, recent timestamps, and Stop All.
4. **Organization** — portable `.cuelet-metadata.json` schema-v2 read/write, conservative v1 backup, editable names/notes/aliases/categories/favorites/shortcuts, and category create/rename/delete workflows.
5. **Windows integration** — global per-sound shortcuts, Enter-to-play search, Escape/Ctrl+F commands, tray/background operation, single-instance CLI forwarding, Mica, native pickers, hidden metadata attributes, and registry-backed user settings that work in packaged and unpackaged builds.
6. **Hardening** — Debug and Release configurations, Windows core regression tests, unsigned development packaging, runtime smoke coverage, settings/about UI, and updated documentation.
7. **Virtual microphone development** — pinned SysVAD build, test-signed Debug package, constrained elevated helper, in-app Audio Setup action, paired endpoint classification, and independent WASAPI render-to-capture testing.

`Cuelet.Core.Win32` compiles toolkit-neutral sound types, stable IDs, scanning, search, filtering, and sorting from `core/cuelet-core`. Windows JSON persistence remains a Windows adapter because the shared `MetadataStore.cpp` currently depends on JSON-GLib.

## Requirements

- Visual Studio Community 2026 18.7 or newer
- MSVC v145 x64 tools
- Windows application development C++ components
- Windows 11 SDK 10.0.26100 or newer
- Developer Mode for local package deployment

The project pins the stable `Microsoft.WindowsAppSDK` NuGet package at `2.2.0`.

## Build and run

From the repository root:

```powershell
powershell -ExecutionPolicy Bypass -File .\apps\windows\scripts\build-windows.ps1 -Configuration Debug
powershell -ExecutionPolicy Bypass -File .\apps\windows\scripts\run-windows.ps1 -NoBuild
```

Debug is intentionally unpackaged, so the generated executable can run directly. Settings live under `HKCU\Software\Cuelet`; library metadata lives in the selected folder.

## Command line

Commands are forwarded to an already running Cuelet instance, so they do not create a second window. Text output is UTF-8; `--json` writes only the requested JSON document to standard output.

```text
Cuelet.exe --help
Cuelet.exe --list-sounds [--json]
Cuelet.exe --list-categories [--json]
Cuelet.exe --play-id <sound-id>
Cuelet.exe --play-name <name>
Cuelet.exe --play-file <path>
Cuelet.exe --stop <sound-id>
Cuelet.exe --stop-all
Cuelet.exe --show
Cuelet.exe --hide
Cuelet.exe --rescan
Cuelet.exe --library <folder>
Cuelet.exe --import <path> [--import <path> ...] [--mode copy|link] [--category <name>] [--json]
Cuelet.exe --reveal-id <sound-id>
Cuelet.exe --create-library <folder>
Cuelet.exe --use-library <folder>
Cuelet.exe --demo
```

`--library <folder>` can be combined with another command. Paths with spaces should be quoted. Exit code `0` indicates success, `2` invalid syntax, `3` missing library/sound/source, `4` a filesystem or metadata failure, and `5` a single-instance forwarding failure.

The WinUI executable uses the Windows GUI subsystem. PowerShell displays its output, but does not always wait before updating `$LASTEXITCODE`; scripts that consume the exit code should use `Start-Process -Wait -PassThru` (and redirect standard output/error when needed).

Release remains the packaged configuration:

```powershell
powershell -ExecutionPolicy Bypass -File .\apps\windows\scripts\build-windows.ps1 -Configuration Release
```

## Tests

```powershell
powershell -ExecutionPolicy Bypass -File .\apps\windows\scripts\test-windows.ps1 -Configuration Debug
powershell -ExecutionPolicy Bypass -File .\apps\windows\scripts\test-windows.ps1 -Configuration Release
```

The Windows test target covers scanning, Unicode paths, stable IDs, search ranking, managed/link import planning, duplicate detection, transactional rename rollback, global shortcut migration, CLI parsing, hidden metadata attributes, startup-library state, notification timeout policy, endpoint ownership/pairing, installer workflow states, and shutdown coordination.

## Package

```powershell
powershell -ExecutionPolicy Bypass -File .\apps\windows\scripts\package-windows.ps1 -Configuration Release
```

This creates an unsigned development MSIX. Signing identity, certificate distribution, Store submission, and production installer policy are release-management work and are intentionally not faked by the development script.

## Current boundaries

- Cuelet contains a pinned-SysVAD driver source overlay, render-to-capture ring,
  test-signed local Debug packaging, a constrained elevated installer helper,
  app integration, and a WASAPI flow verifier. This is not a public production
  package; Microsoft-compatible production driver signing is still required.
  See `docs/VIRTUAL_AUDIO_DRIVER.md`.
- The package still uses development visual assets; final brand artwork belongs
  in the release/signing pass.
