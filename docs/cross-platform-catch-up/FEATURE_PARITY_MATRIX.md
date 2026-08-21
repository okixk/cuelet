# Cuelet feature parity matrix

This matrix is a validation classification, not a claim that all platforms
were run in the same session. Results come from the platform validation records
in this directory and the current native implementations. Historical runtime
evidence is not presented as a new run on another host.

Evidence labels:

- **Runtime** — exercised on the named platform with a real application or
  endpoint;
- **Automated** — covered by the platform's tests or verifier;
- **Source** — implementation inspected but not runtime-proven here;
- **Docs** — behavior stated by documentation only;
- **Partial** — some behavior works but a required part is absent or not
  verified;
- **Missing** — no implementation/evidence found; and
- **N/T** — not testable from this host or not retested in the named final cycle.

| Area | Capability | macOS | Linux | Windows | User-facing parity / note |
|---|---|---|---|---|---|
| Library/data | Managed import | Runtime + Automated | Runtime + Automated | Runtime + Automated | Copy mode creates Cuelet-owned media on all three. |
| Library/data | Linked import | Runtime + Automated | Runtime + Automated | Runtime + Automated | External source preservation is explicit. |
| Library/data | Durable metadata | Runtime + Automated | Runtime + Automated | Runtime + Automated | Schema and adapter details differ; stable IDs are intended to persist. |
| Library/data | Stable sound IDs | Automated + Runtime | Runtime + Automated | Runtime + Automated | Rename does not change shortcut/metadata identity. |
| Library/data | Missing-file preservation | Runtime + Automated | Runtime + Automated | Runtime + Automated | Missing entries remain visible and recoverable. |
| Library/data | Relink | Runtime + Automated | Runtime + Automated | Source + Docs | Windows source has relink flows; final Windows runtime evidence is not in this Mac session. |
| Library/data | Favorites/categories/notes/aliases | Runtime + Automated | Runtime + Automated | Runtime + Automated | Native UI and persistence are platform-specific. |
| Library/data | Remove from library | Runtime + Automated | Runtime + Automated | Runtime + Automated | Metadata-only removal preserves audio. |
| Library/data | Delete managed file | Runtime + Automated | Runtime + Automated | Source + Automated | Requires separate destructive confirmation and safety revalidation. |
| Library/data | Linked-file preservation | Runtime + Automated | Runtime + Automated | Source + Automated | Linked sources are not deleted by library removal. |
| Library/data | Duplicate handling | Runtime + Automated | Runtime + Automated | Runtime + Automated | Duplicate identity/collision handling is covered. |
| Library/data | Symlink/platform safety | Automated | Runtime + Automated | Automated | The safety primitive is platform-specific. |
| Library/data | Atomic persistence/migration/backup | Runtime + Automated | Runtime + Automated | Runtime + Automated | Private settings/storage formats differ. |
| Playback | Play/pause/resume/stop | Runtime + Automated | Runtime + Automated | Runtime + Automated | macOS GUI playback and exact-candidate virtual-input capture passed in the final release cycle. |
| Playback | Stop All | Runtime + Automated | Runtime + Automated | Runtime + Automated | All three expose explicit stop-all behavior. |
| Playback | Multiple simultaneous sounds | Runtime + Automated | Runtime + Automated | Runtime + Automated | Headroom/mixing implementation differs. |
| Playback | Progress/duration/mini-player | Runtime + Automated | Runtime + Automated | Runtime + Automated | Native controls differ. |
| Playback | Global volume | Runtime + Automated | Runtime + Automated | Runtime + Automated | macOS overlap uses conservative per-player headroom. |
| Playback | Explicit output selection | Runtime + Automated | Runtime + Automated | Runtime + Automated | Stable UID/device identifiers differ. |
| Playback | Device-loss behavior | Automated + Source | Runtime + Automated | Runtime + Automated | macOS injectable loss/reconnect; Linux/Windows have runtime records. |
| Playback | Speaker-only route | Runtime + Automated | Runtime + Automated | Runtime + Automated | Default/physical destination remains available. |
| Playback | Virtual-only route | Runtime + Automated | Runtime + Automated | Runtime + Automated | macOS uses its HAL transport, Linux uses an app-owned PipeWire route, and the Windows Release app uses a separately installed VB-CABLE pair. |
| Playback | Speaker-plus-virtual route | Missing | Runtime + Automated | Runtime + Automated | macOS intentionally supports one destination at a time. |
| Playback | Physical-microphone mixing | Unsupported for 0.1.0 | Runtime + Automated | Implemented; not retested in final cycle because Windows microphone access was disabled | macOS does not claim this capability; Linux live evidence covers the selected-source path. |
| Shortcuts | Local shortcuts | Runtime + Automated | Runtime + Automated | Runtime + Automated | Native keyboard APIs differ. |
| Shortcuts | Global shortcuts | Runtime + Automated | Runtime + Automated | Runtime + Automated | Linux uses the portal/session model; macOS Carbon; Windows global registration. |
| Shortcuts | Stable identity across rename | Runtime + Automated | Runtime + Automated | Runtime + Automated | UUID-based behavior is aligned. |
| Shortcuts | Conflict handling | Runtime + Automated | Runtime + Automated | Runtime + Automated | Conflict UI wording is platform-native. |
| Shortcuts | Restore after restart | Runtime + Automated | Runtime + Automated | Runtime + Automated | Linux portal tokens/session behavior is distinct. |
| Shortcuts | Deleted-sound behavior | Automated | Runtime + Automated | Automated | Old IDs become inert rather than targeting another sound. |
| Virtual audio | Virtual microphone availability | Installed/runtime | Runtime | Runtime | Evidence is platform-specific: Cuelet HAL device, app-owned PipeWire graph, or separately installed VB-CABLE endpoints. |
| Virtual audio | Installation requirement | Runtime + Docs | Runtime + Docs | Runtime + Docs | macOS uses an administrator-installed Cuelet package plus restart; Linux uses transient user-session helpers; Windows Release relies on VB-CABLE's vendor install and restart. |
| Virtual audio | Root/admin requirement | Runtime + Docs | Runtime + Docs | Runtime + Docs | No privilege escalation in Linux runtime path. |
| Virtual audio | Restart requirement | Runtime | N/A for transient nodes | Runtime | macOS/Windows driver lifecycle differs from Linux app-owned nodes. |
| Virtual audio | Input/output pairing | Runtime + Automated | Runtime + Automated | Runtime + Automated | All expose paired injection/capture concepts. |
| Virtual audio | Multi-client capture | Runtime + Automated | Runtime + Automated | Runtime + Automated | The implementations use platform-native client and endpoint models. |
| Virtual audio | Physical-mic mixing | Unsupported | Runtime + Automated | Implemented; not retested in final cycle | Windows latest-cycle microphone access was disabled; VB-CABLE is separately installed and not redistributed. |
| Virtual audio | Speaker monitoring | Unsupported / not claimed | Runtime + Automated | Runtime + Automated | macOS does not claim simultaneous local monitoring. |
| Virtual audio | Runtime cleanup | Runtime + Docs | Runtime + Automated | Runtime + Automated | macOS HAL and Windows VB-CABLE endpoints persist after app exit; Linux nodes/helpers disappear. |
| Virtual audio | Production signing | Missing | N/A | Missing | macOS app/driver and Windows MSIX still need production identities and signing; Linux has no kernel driver to sign. |
| Virtual audio | Sandboxed distribution | Docs only | Docs | Docs | No production Store/App Store/notarized package claim. |
| UI | Grid/list/navigation | Runtime + Automated | Runtime + Automated | Runtime + Automated | Native implementations differ. |
| UI | Context menus | Runtime + Automated | Runtime + Automated | Runtime + Automated | Wording and native menu surfaces differ. |
| UI | Missing-file UI | Runtime + Automated | Runtime + Automated | Runtime + Automated | Preserve visibility and recovery actions. |
| UI | Import-mode selection | Runtime + Automated | Runtime + Automated | Runtime + Automated | Copy/link semantics are documented. |
| UI | Mini-player/routing settings | Runtime + Automated | Runtime + Automated | Runtime + Automated | macOS driver status is now live-verified. |
| UI | Driver status/diagnostics | Runtime + Automated | Runtime + Automated | Runtime + Automated | Status models are platform-specific and should remain so. |
| UI | Accessibility | Partial | Partial | Partial | Important labels exist; complete AT/VoiceOver/Narrator audits remain open. |
| Packaging | Debug/Release build | Runtime + Automated | Runtime + Automated | Runtime + Automated | Packaging toolchains differ. |
| Packaging | Local release artifact | Automated; unsigned local-only test package | Automated; reproducible x86_64 tar archive | Automated; unsigned local MSIX | macOS and Windows publication remains blocked by production signing; Linux has no driver-signing requirement. |

## Highest-value gaps

1. macOS physical-microphone mixing and simultaneous speaker-plus-virtual
   routing are intentionally absent.
2. Windows and macOS production signing/distribution are not complete.
3. Broader receiving-application and sleep/wake compatibility evidence is
   incomplete on macOS.
4. Linux receiving-application coverage beyond `pw-record` remains limited.
5. Accessibility audits are partial on every platform even though important
   controls have labels and native semantics.
