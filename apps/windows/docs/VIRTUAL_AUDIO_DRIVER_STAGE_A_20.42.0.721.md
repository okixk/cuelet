# Stage A runtime report: Cuelet virtual audio 20.42.0.721

Date: 2026-07-21  
Machine: daily-use Windows 11 24H2 development laptop  
Result: **FAILED during root-devnode start; fully removed**  
Stage B: **blocked**

No Cuelet application or audio endpoint was opened. Driver Verifier and WinDbg
were not used.

## Evidence

Evidence directory:

`apps\windows\x64\Debug\StageA-20.42.0.721-20260721-213304`

The directory contains the exact isolated bundle, elevated transcripts,
command ledger, full and isolated SetupAPI logs, System/Application event
exports, Code Integrity event export, PnP/driver-store/service snapshots, and
preflight/final JSON results.

| Evidence artifact | SHA-256 |
|---|---|
| `stage-a-result.json` | `7C5B3C7169C84526C250B8ADDBC170494711BA2E2A884D105D4FA0A1321BE2E7` |
| `installer-install.json` | `41B221B1B64693889782CF9BB3F7C858A50E0134717608C528163055E7548137` |
| `setupapi-stage-a-section.txt` | `B48BE4F5300E45E142AE0BC283E9CE64892FB937DA0C11DD2C0E1539888311F0` |
| `events-relevant.json` | `B93E0691AB6C2F8490E2A07B062E9134F114E96286D956BF665507641DC31402` |
| `commands-executed.txt` | `48FF0D911E6EE5B20F8771C8E0B5F2971C5D26DA2587A5F7DF2ACBAB9E9CB68E` |

## Elevated preflight

The preflight passed before any installation:

- all five candidate hashes matched the hash-locked manifest;
- INF `DriverVer` was exactly `07/21/2026,20.42.0.721`;
- the isolated helper resolved only the adjacent `20.42.0.721` bundle;
- no Cuelet or retired driver was present in the driver store;
- no Cuelet service, devnode, endpoint, process, or loaded module existed;
- Driver Verifier flags were zero and no drivers were selected;
- the exact development signer was in LocalMachine Root and TrustedPublisher;
- test-signing was active and Secure Boot was disabled;
- `fsutil dirty query C:` reported `Volume - C: is NOT Dirty`; and
- volume C: reported NTFS, Healthy, and OK.

The event baseline was `2026-07-21T19:37:58.5022170Z`, System record 1999 and
Application record 464. Exact emergency uninstall and ownership-validated
fallback cleanup commands were printed and preserved before installation.

## Install attempt

The isolated Debug helper rechecked all hashes and invoked:

```text
Cuelet.VirtualAudio.Installer.exe install --allow-test-package
```

Windows staged the package successfully:

- published INF: `oem5.inf`;
- DriverStore directory:
  `cueletvirtualaudio.inf_amd64_3ebec0f8f7733230`;
- root instance: `ROOT\CUELETVIRTUALAUDIO\0000`;
- configured service: `cuelet_virtual_audio`;
- configured image:
  `\SystemRoot\System32\DriverStore\FileRepository\cueletvirtualaudio.inf_amd64_3ebec0f8f7733230\CueletVirtualAudio.sys`.

The package source SYS rechecked immediately before installation as:

`0597827EDF67A9D26AFA393F39399C77BAA81298A1E6AE1ED82D57A7FEDA2EA3`

At 21:41:15 local time, PnP attempted to start the root device. SetupAPI then
recorded:

```text
Device not started: Device has problem: 0x0a (CM_PROB_FAILED_START),
problem status: 0xc000000d.
```

`0xC000000D` is `STATUS_INVALID_PARAMETER`. The driver did not reach endpoint
creation. Therefore neither expected endpoint existed and no endpoint parent,
container, or pairing relationship was available to verify.

The helper waits for the complete endpoint pair. After 30 seconds it returned
exit code 15 and reported that it rolled back the partial installation. Because
rollback completed before control returned, no persistent loaded module was
available for a live destination-file hash. SetupAPI proves that the exact
hash-verified package was the staged source and records the configured image
path, but this result must not be described as a successful loaded-SYS hash
verification.

## Events

From the recorded baseline through final cleanup, System and Application
contained three new events and zero warning/error events:

- System, Service Control Manager 7045, Information: Cuelet kernel service
  installed with the expected DriverStore image path;
- System, Service Control Manager 7040, Information: service start type changed
  from demand start to disabled during rollback; and
- one unrelated AppX deployment informational event.

There were no BugCheck, WER, Kernel-PnP warning/error, DriverFrameworks, Audio,
NTFS, Disk, stornvme, storport, or WHEA warning/error events. The Code Integrity
Operational query returned no new events. There was no bugcheck or crash.

## Rollback and cleanup

The installer rollback removed `ROOT\CUELETVIRTUALAUDIO\0000` and unpublished
and deleted `oem5.inf`, its catalog, and its DriverStore directory. The explicit
idempotent helper uninstall then returned exit code 0.

The first cleanup audit found one stopped, disabled service residue with exact
Cuelet ownership. It had `DriverDelete=1`, `DeleteFlag=1`, and still referenced
the already-removed 20.42 DriverStore directory. After verifying those fields,
`sc.exe delete cuelet_virtual_audio` succeeded. A repeated elevated audit then
proved all of the following are absent:

- Cuelet/retired driver-store package;
- `oem5.inf` and its DriverStore directory;
- root devnode or non-present phantom;
- render or capture endpoint;
- PnP signed-driver record;
- service, service registry key, or loaded system-driver record;
- loaded module in `driverquery`; and
- Cuelet process.

Driver Verifier remains disabled, C: remains not dirty, and C: remains
Healthy/OK.

## Conclusion

Stage A did not pass. Candidate `20.42.0.721` must not be reinstalled and Stage
B must not begin. The next development task is to identify which DriverEntry,
AddDevice, or StartDevice path returns `STATUS_INVALID_PARAMETER`, fix that
defect, repeat build/static/package validation under a new driver version, and
then restart at Stage A.
