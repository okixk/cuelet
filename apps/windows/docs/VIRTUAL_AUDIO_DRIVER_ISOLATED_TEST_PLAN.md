# Isolated test plan for Cuelet virtual-audio candidate 20.42.0.721

> **Runtime result, 2026-07-21:** Stage A failed. The root devnode returned
> `CM_PROB_FAILED_START` with `STATUS_INVALID_PARAMETER (0xC000000D)` before
> creating endpoints. The package was fully removed. Do not reinstall this
> candidate and do not begin Stage B; see
> `VIRTUAL_AUDIO_DRIVER_STAGE_A_20.42.0.721.md`.

Run this candidate first on a disposable secondary Windows installation or an
isolated VM that supports Windows audio endpoints, WaveRT timing, kernel dumps,
and snapshots. The daily-use development laptop is only a later fallback after
the isolated stages pass and the explicit laptop preflight below is complete.
Never enable Driver Verifier on the daily-use laptop.

Any Driver Verifier bugcheck is a test failure. Never select all drivers.

## Test asset

Candidate:

`DriverPackage-20.42.0.721-Candidate`

Verify these hashes before moving the package and again on the test machine:

```text
CueletVirtualAudio.sys  0597827EDF67A9D26AFA393F39399C77BAA81298A1E6AE1ED82D57A7FEDA2EA3
CueletVirtualAudio.pdb  E6AEB8E1811F8B24E1838FA1CD514B5DB24F9AD30E964ED78415C564B806B8E8
CueletVirtualAudio.inf  B37CF2990B5777852562CE1D3DEC367AA2A893151A27F0598EE6432C679D3CAA
cueletvirtualaudio.cat  535B5ECC20DEDD99BE98C4B284C42AF991699D91E82288EEA1F412A801CC2DE1
candidate-manifest.json 0175838E3F15ABAC0EB3DB990357016E1F6A8F7AF7EEFE7E00F079D1F5E58C76
```

Use `Get-FileHash -Algorithm SHA256`. Stop if any value differs.

## Daily-use laptop preflight

Do not start a laptop kernel test merely because the package exists. Before the
first installation on that machine:

1. clearly announce that the kernel-driver test is beginning;
2. confirm the repository and important personal files have current off-device
   backups;
3. confirm no Cuelet devnode, driver-store package, service, endpoint, or loaded
   SYS remains;
4. preserve the previous dumps and their exact SYS/PDB/INF/CAT artifacts;
5. write down the exact uninstall and manual rollback commands for the package
   selected on that machine;
6. require NTFS to be clean and volume C: to report Healthy/OK; and
7. close unrelated workloads and do not run heavy diagnostics during the test.

If any preflight item is uncertain, use the disposable system instead.

## Before installation

1. Use a machine with no important data. Make a full backup and a VM snapshot
   or restorable disk image.
2. Create bootable Windows recovery media and verify that WinRE and Safe Mode
   are accessible.
3. Record the Windows build, Secure Boot, Memory Integrity, audio-controller
   driver, BIOS, and VM hypervisor versions.
4. Configure a system-managed page file on the Windows volume.
5. Configure a kernel or complete memory dump and verify sufficient free disk
   space. Preserve `%SystemRoot%\MEMORY.DMP`,
   `%SystemRoot%\Minidump`, and `%SystemRoot%\inf\setupapi.dev.log` after a
   failure.
6. Copy the candidate package, its PDB, the exported development certificate,
   the matching Debug installer helper, tests, symbols, and this plan to the
   test machine.
7. Put the candidate files in the test bundle's expected
   `x64\Debug\DriverPackage` directory. Do not mix files from another build.
8. From an elevated PowerShell on the test machine only, trust the development
   certificate in `LocalMachine\Root` and
   `LocalMachine\TrustedPublisher`, then enable test signing:

```powershell
Import-Certificate .\CueletVirtualAudioDevelopment.cer `
  -CertStoreLocation Cert:\LocalMachine\Root
Import-Certificate .\CueletVirtualAudioDevelopment.cer `
  -CertStoreLocation Cert:\LocalMachine\TrustedPublisher
bcdedit.exe /set testsigning on
```

Restart and confirm Test Mode before installing. If Secure Boot prevents test
signing, change that policy only on the disposable test system and record the
change. Do not weaken the development laptop.

## Baseline and install

Before each scenario, capture:

```powershell
Get-PnpDevice -PresentOnly:$false |
  Where-Object {
    $_.InstanceId -like 'ROOT\CUELETVIRTUALAUDIO*' -or
    $_.FriendlyName -like '*Cuelet Virtual*'
  } | Format-List *

pnputil /enum-drivers |
  Select-String -Pattern 'Cuelet|CueletVirtualAudio' -Context 3,6

Get-CimInstance Win32_SystemDriver |
  Where-Object {
    $_.Name -match 'Cuelet' -or
    $_.PathName -match 'CueletVirtualAudio'
  } | Format-List *

verifier /querysettings
```

The baseline must contain no Cuelet component and no verifier selection.

Run the matching installer helper elevated:

```powershell
$env:CUELET_ALLOW_TEST_DRIVER = '1'
.\Cuelet.VirtualAudio.Installer.exe install --allow-test-package
```

Confirm that exactly one `ROOT\CUELETVIRTUALAUDIO` root devnode, the intended
render/capture endpoints, one Cuelet driver-store package, and service
`cuelet_virtual_audio` exist. Confirm that the installed INF reports
`20.42.0.721`. Do not continue if Windows selects a different package.

## Staged campaign

Complete one stage and check stability and system events before starting the
next. A later stage never converts an earlier abnormality into a pass.

### Stage A: lifecycle without audio

- Install the driver and verify the package, service, root devnode, and both
  endpoints.
- Do not open either audio endpoint and do not play audio.
- Wait briefly, inspect System and Application events, and re-run the baseline
  state queries.
- Uninstall immediately and restart if anything is abnormal.

### Stage B: stream creation and destruction

- Reinstall the same hash-verified package.
- Open and close render and capture individually and together.
- Repeat stream creation/destruction, app exit, device disable/enable, clean
  uninstall, and restart behavior.
- Require endpoints, service, reader/writer counts, and teardown state to
  return cleanly after every operation.

### Stage C: short reference tone

- Render a short 997 Hz signal and capture it through the paired endpoint.
- Record detected frequency, continuity, clipping, latency, and dropout count.
- Stop on repeated zero quanta, discontinuity, teardown failure, or an
  unexplained service/application crash.

### Stage D: low-frequency and program material

- Test 50, 80, 100, and 200 Hz, then sweeps and real music containing bass.
- Detect repeated zero blocks, duplicated frames, dropped quanta, phase jumps,
  discontinuities, clipping, and drift.

### Stage E: endurance and hostile lifecycle

- Increase playback duration gradually and exercise rapid start/stop cycles.
- Force-close user-mode clients while their streams are open.
- Exercise disable/enable, uninstall/reinstall, and restart cycles.
- Preserve the exact iteration and preceding operation for every anomaly.

## Targeted Driver Verifier

Only on the disposable secondary system, first complete Stages A and B without
Verifier. Then snapshot the machine and enable Verifier for only the Cuelet
SYS. Skip this entire section on the daily-use laptop:

```powershell
verifier /reset
verifier /flags 0x0000093B /driver CueletVirtualAudio.sys
verifier /querysettings
```

`0x93B` selects Special Pool, Force IRQL Checking, Pool Tracking, I/O
Verification, Deadlock Detection, Security Checks, and Miscellaneous Checks.
The query output must name only `CueletVirtualAudio.sys`. If any other driver
is selected, immediately run `verifier /reset` and restart.

Restart after changing verifier settings. Keep kernel symbols and the exact
candidate PDB available. Do not use `/all`, “all drivers,” or random low
resources during the first campaign.

## Scenario matrix

Run each scenario first without verifier and then from a clean snapshot with
targeted verifier:

- fresh install;
- upgrade from the preserved `17.19.33.611` package;
- repair/reinstall of the same candidate;
- uninstall while idle;
- verifier-enabled uninstall;
- 100 enable/disable cycles;
- 1,000 endpoint open/close cycles;
- render only;
- capture only;
- simultaneous render and capture;
- continuous 40 Hz playback;
- 80, 100, 440, and 997 Hz plus multitone;
- broadband playback;
- long-duration playback, initially 8 hours and then 24 hours;
- stream reset while active;
- Cuelet process exit while streaming;
- root-device removal while streaming;
- sleep/resume for at least 50 cycles;
- rapid AudioGraph recreation;
- repeated endpoint refresh;
- repeated install/upgrade/uninstall cycles.

For audio integrity, compare capture against render at the byte/frame level
where possible and record discontinuities, zero quanta, dropped frames,
startup latency, and overflow recovery. Preserve the bridge's restrained
creation/teardown summaries; do not enable per-frame logging.

After every destructive PnP operation, verify:

- no callback executes after its stream destructor completes;
- bridge callback entry and exit counts are balanced at teardown;
- active reader and writer counts are zero;
- later operations are rejected after teardown begins;
- reader count returns to zero;
- no pool, IRQL, lock, deadlock, or I/O verifier violation occurred.

## Normal uninstall

Use the exact matching helper first:

```powershell
.\Cuelet.VirtualAudio.Installer.exe uninstall
verifier /reset
shutdown /r /t 0
```

After restart, repeat the baseline inspection and require no Cuelet devnode,
endpoint, driver-store package, service, loaded SYS, process, or verifier
selection.

## Manual removal fallback

Find the exact root-device instance and OEM INF:

```powershell
pnputil /enum-devices /deviceid "ROOT\CUELETVIRTUALAUDIO"
pnputil /enum-drivers |
  Select-String -Pattern 'Cuelet|CueletVirtualAudio' -Context 3,6
```

Remove only the returned Cuelet instance and exact Cuelet OEM INF:

```powershell
pnputil /remove-device "ROOT\CUELETVIRTUALAUDIO\0000"
pnputil /delete-driver oemNN.inf /uninstall /force
verifier /reset
```

Replace the instance ID and `oemNN.inf` with values obtained on that test
machine. Never guess an OEM number and never delete another driver's package.
If package removal completed but a stale service remains, verify its binary
path first, then remove only the Cuelet service:

```powershell
sc.exe qc cuelet_virtual_audio
sc.exe delete cuelet_virtual_audio
```

Restart and inspect again.

## Safe Mode recovery

From WinRE choose Troubleshoot, Advanced options, Startup Settings, Restart,
then Safe Mode. In elevated Safe Mode:

```powershell
verifier /reset
pnputil /enum-devices /deviceid "ROOT\CUELETVIRTUALAUDIO"
pnputil /enum-drivers |
  Select-String -Pattern 'Cuelet|CueletVirtualAudio' -Context 3,6
pnputil /remove-device "<exact Cuelet instance ID>"
pnputil /delete-driver <exact Cuelet oemNN.inf> /uninstall /force
shutdown /r /t 0
```

If a forced Safe Mode setting was used, remove it before the final restart:

```powershell
bcdedit /deletevalue {current} safeboot
```

## Boot-loop recovery

If Safe Mode cannot start:

1. Boot WinRE Command Prompt.
2. Identify the Windows volume; it may not be `C:` in WinRE.
3. Disable verifier for the next boot:

```text
verifier /reset
```

4. If necessary, disable only the offline Cuelet service:

```text
reg load HKLM\CueletOffline X:\Windows\System32\Config\SYSTEM
reg add HKLM\CueletOffline\ControlSet001\Services\cuelet_virtual_audio /v Start /t REG_DWORD /d 4 /f
reg unload HKLM\CueletOffline
```

Replace `X:` with the verified offline Windows volume. If the active control
set differs, inspect `HKLM\CueletOffline\Select` and edit only the selected
control set.

5. After the system boots into Safe Mode, identify the exact Cuelet
   `oemNN.inf` and remove it with `pnputil` as above. If Windows cannot boot,
   use offline DISM only after identifying the exact package:

```text
dism /Image:X:\ /Get-Drivers /Format:Table
dism /Image:X:\ /Remove-Driver /Driver:oemNN.inf
```

Never remove a package based only on its OEM number.

## Failure collection

On any bugcheck or verifier finding:

- stop the scenario; do not treat a later successful boot as a pass;
- preserve the full `MEMORY.DMP` before another crash overwrites it;
- preserve minidumps, SetupAPI logs, System/Application event logs, verifier
  query output, PnP state, OEM INF, installed SYS/CAT, and the exact PDB;
- hash all artifacts;
- record the operation and timing immediately before failure;
- move dumps and matching symbols to another analysis machine; do not run
  WinDbg against large dumps on the development laptop;
- on that other machine, run WinDbg with Microsoft symbols plus the exact
  candidate PDB;
- collect `!analyze -v`, `.bugcheck`, `kv`, `lmvm CueletVirtualAudio`,
  `!thread`, `!irql`, `!locks`, `!verifier 3 CueletVirtualAudio.sys`,
  relevant `!pool`, `!poolval`, `!pte`, `!address`, and disassembly.

Do not continue the campaign until the failure is understood and a new,
separately versioned candidate has been built.
