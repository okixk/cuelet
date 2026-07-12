# Cuelet for Windows

Cuelet for Windows is a native WinUI 3 and C++/WinRT application. Debug builds run unpackaged for a quick edit/build/run loop; Release builds use a single-project packaged MSIX configuration.

## Implemented phases

1. **Foundation** — Windows App SDK 2.2, C++/WinRT, XAML, Mica, integrated title bar, `NavigationView`, build/run/package scripts, and the shared-core MSVC static library.
2. **Library** — persisted folder selection, recursive scanning through `cuelet-core`, search, favorites/recent/category scopes, sorting, grid/list views, import, rescan, missing-file handling, and useful empty/error states.
3. **Playback** — native `MediaPlayer` playback, overlapping or single-sound mode, live volume changes, duration discovery, playback progress, recent timestamps, and Stop All.
4. **Organization** — portable `.cuelet-metadata.json` schema-v2 read/write, conservative v1 backup, editable names/notes/aliases/categories/favorites/shortcuts, and category create/rename/delete workflows.
5. **Windows integration** — app-local per-sound shortcuts, Space/Enter/Escape/Ctrl+F commands, single-instance redirection, command-line folder activation, Mica, native pickers, and registry-backed user settings that work in packaged and unpackaged builds.
6. **Hardening** — Debug and Release configurations, Windows core regression tests, unsigned development packaging, runtime smoke coverage, settings/about UI, and updated documentation.

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

Release remains the packaged configuration:

```powershell
powershell -ExecutionPolicy Bypass -File .\apps\windows\scripts\build-windows.ps1 -Configuration Release
```

## Tests

```powershell
powershell -ExecutionPolicy Bypass -File .\apps\windows\scripts\test-windows.ps1 -Configuration Debug
powershell -ExecutionPolicy Bypass -File .\apps\windows\scripts\test-windows.ps1 -Configuration Release
```

The Windows test target covers recursive/non-recursive scanning, unsupported-file reporting, stable IDs, deterministic category IDs, filename display names, and composed search/favorite filters.

## Package

```powershell
powershell -ExecutionPolicy Bypass -File .\apps\windows\scripts\package-windows.ps1 -Configuration Release
```

This creates an unsigned development MSIX. Signing identity, certificate distribution, Store submission, and production installer policy are release-management work and are intentionally not faked by the development script.

## Current boundaries

- Shortcuts are active while Cuelet has keyboard focus; global registration and its conflict/permission UX remain future work.
- Playback uses the Windows default output. A device selector and virtual-microphone routing need a separate Windows audio-routing design.
- The package still uses development visual assets; final brand artwork belongs in the release/signing pass.
