# Cuelet virtual-audio runtime result: 20.43.0.721

Date: 2026-07-24  
Target: Windows 11 x64, local controlled test-signing environment  
Pinned SysVAD source: `2ee527bfeb0aeb6be11f0a8b6dce4011b358ce89`

## Outcome

Candidate `20.43.0.721` passed Stages A, B, C, and D. It created both
endpoints, survived the bounded lifecycle tests and a devnode disable/enable
cycle, and preserved tones, a sweep, broadband fixtures, a bass-heavy fixture,
and actual musical audio without periodic gaps, duplicate runs, clipping, or
active zero-filled quanta.

The candidate was uninstalled after testing. The final audit found no package,
devnode, endpoint, loaded driver, service, service key, DriverStore SYS, or
test process. C: was not dirty and remained Healthy/OK. There were no new
storage/hardware, bugcheck/live-kernel, or candidate Code Integrity errors.

Stage E was deliberately not started in this run.

## Exact 20.42 startup failure

The operation that produced `STATUS_INVALID_PARAMETER` was:

```cpp
unknownWave->QueryInterface(
    IID_IPortClsRuntimePower,
    reinterpret_cast<void**>(&pPortClsRuntimePower));
```

It is an optional capability probe in `InstallEndpointRenderFilters`, after
the render topology and wave subdevices have already installed successfully.
It returned `0xC000000D` on this normal PortCls render endpoint.

The functional `InstallEndpointFilters` result and the optional-probe results
shared the same `ntStatus` local. The 20.42 hardening work removed two
sample-only `IPortClsStreamResourceManager` exercises that occurred after the
runtime-power probe. In the last endpoint-producing source, those later
sample tests happened to overwrite the optional failure with success.
Removing them exposed the pre-existing status-coupling bug: the optional
runtime-power result became the function's return status even though endpoint
installation itself had succeeded.

The pinned stock SysVAD implementation has the same sample-oriented sequence
and status reuse. This was therefore an interaction between a latent stock
sample assumption and Cuelet's removal of non-production test operations, not
an invalid root-device resource list, FIFO geometry, format, DMA buffer, timer,
bridge epoch, adapter pointer, or endpoint descriptor.

The targeted fix preserves `ntStatus` exclusively for the functional endpoint
installation result and uses `optionalStatus` for ETW, runtime-power, and
other optional probes. No format, FIFO, resource, lifetime, or integer bound
was relaxed.

## Startup trace proof

Debug builds use a diagnostic WPP provider with GUID
`{1819CEB3-B714-493F-8B5F-771AFFB0DC63}`. The provider has 74 authored
checkpoint identifiers covering DriverEntry, AddDevice, PnP transitions,
StartDevice, resource lists, adapter initialization, bridge lifecycle, FIFO
setup, endpoint/subdevice installation, interfaces, registry operations,
WaveRT buffer allocation, notification parameters, timer/DPC creation and
arming, partial cleanup, stop, and unload. Release builds compile the
checkpoint calls out.

The successful Stage A trace contained 291 events, 287 checkpoint lines, 65
unique checkpoints, zero lost events, and zero formatting errors. The
decisive sequence was:

```text
CVA122 InstallEndpointFilters render                 STATUS_SUCCESS
CVA123 optional QueryInterface IPortClsEtwHelper     STATUS_SUCCESS
CVA124 optional QueryInterface IPortClsRuntimePower  0xC000000D
CVA129 InstallEndpointRenderFilters exit             STATUS_SUCCESS
CVA130 render endpoint installed                     STATUS_SUCCESS
CVA150 capture endpoint installed                    STATUS_SUCCESS
CVA160 bridge prepared for device start              STATUS_SUCCESS
CVA199 StartDevice exit                              STATUS_SUCCESS
```

The `CVA129` auxiliary value retains the optional `0xC000000D`, proving that
the expected optional failure still occurs while the functional status is no
longer corrupted.

Other startup facts recorded by the trace include:

- root-enumerated raw and translated resource-list counts were both zero and
  were accepted;
- `PcAddAdapterDevice`, adapter-common creation, physical-device lookup, WDF
  miniport creation, subdevice registration, interface registration, and
  topology/wave connection all returned success;
- one render endpoint and one capture endpoint were installed;
- render descriptor: two maximum channels, five format/mode entries, one
  physical connection, and one wave-interface property;
- bridge storage: 524,288 bytes, 32 readers maximum, and a 30 ms target
  reserve;
- observed WaveRT format: 48,000 Hz, two channels, 16 bits, four-byte block
  alignment, and 192,000 bytes/second;
- observed notification buffer: 3,840 bytes, two notifications, 20 ms buffer
  duration, and 10 ms notification interval; and
- timer and DPC allocations and stream creation returned success.

Tracing is confined to diagnostic lifecycle/initialization paths. It does not
emit continuously from the ordinary real-time audio-copy path.

## Candidate and validation

Only one new driver candidate was needed:

| Candidate | Result |
| --- | --- |
| `20.43.0.721` | Passed Stages A-D |

The earlier `20.42.0.721` package was not reinstalled. The
`StageA-20.43.0.721-20260724-140317` directory is a pre-install trace-session
preflight only: `tracelog` rejected an invalid `0xffffffff` flag mask, no
package or devnode was installed, and the WPP flag was corrected to `0x3`.
The actual Stage A evidence is
`StageA-20.43.0.721-20260724-140524`.

After that installation succeeded, the initial collector stopped because
Windows PowerShell 5.1 promoted `tracepdb` informational stderr to a terminating
pipeline error. The completion collector decoded the already captured ETL and
finished the same Stage A evidence set without reinstalling the candidate.
This was an automation/collection issue, not a driver failure.

The immutable, read-only candidate directory is
`apps/windows/x64/Debug/DriverPackage-20.43.0.721-Candidate`.

Hashes:

| File | SHA-256 |
| --- | --- |
| `CueletVirtualAudio.sys` | `B99FAB723EF9C0FF39CBC7CD0D34954C07D06341766D65FC64D5A529DCF902B3` |
| `CueletVirtualAudio.pdb` | `7666EBDE2968644CB13B4FC0A2D5E3FA77DFE18C1BBD7BC17EB579E9FDB340A9` |
| `CueletVirtualAudio.inf` | `DC20D7DB9A9A9E3F524C060D2C83AE7128278B447707750AC45EA71A67C9D738` |
| `cueletvirtualaudio.cat` | `8481EA8F85787653C89E38065E9C0DBC352096662C86AB3A0804CF00FB7ACDE6` |
| `candidate-manifest.json` | `E80359D45A8CD4CE7F8BF85619CB9751692450F121C7620A8FF1951CD133D3BA` |

The PDB signature was
`{69ECBF1C-D986-49A6-B8F8-8107A5857D87}`, age 1. The local development
certificate SHA-1 was
`D1DA73ABCD80DBC1564D5304564DD24575BF758B`.

Completed validation:

- Debug and Release driver rebuilds with warnings as errors;
- Debug and Release full Windows solution builds with warnings as errors;
- Debug and Release core tests;
- MSVC AddressSanitizer user-mode core tests with the matching ASan runtime;
- WDK `/analyze` with zero Cuelet-authored warnings (three inherited SysVAD
  `AudioModule` C6387 warnings remained);
- InfVerif Windows and Universal modes;
- Inf2Cat with zero errors and warnings;
- Universal ApiValidator;
- SignTool policy verification for SYS and CAT;
- symchk confirmation that the SYS matches the private PDB;
- packaged-versus-built SYS hash comparison; and
- retired `20.37.42.726` selection-policy regression testing.

InfVerif continued to report 42 inherited warning 2083 notices for unreachable
stock SysVAD sections. They were preserved as known upstream notices rather
than hidden.

Primary evidence:

- candidate:
  `apps/windows/x64/Debug/DriverPackage-20.43.0.721-Candidate`;
- complete Stage A:
  `apps/windows/x64/Debug/StageA-20.43.0.721-20260724-140524`;
- decoded startup trace:
  `StageA-20.43.0.721-20260724-140524/cuelet-startup-trace.txt`;
- Stages B-D and cleanup:
  `apps/windows/x64/Debug/Runtime-20.43.0.721-20260724-141240`;
- WDK analysis:
  `apps/windows/x64/Release/Development-20.43.0.721/wdk-analyze-release.log`;
- final cleanup:
  `Runtime-20.43.0.721-20260724-141240/final-cleanup/final-cleanup-result.json`;
  and
- SHA-256 inventories named `evidence-hashes.sha256` in each Stage A, runtime,
  Debug-development, and Release-development evidence root.

## Stage A

Stage A passed with Code 0 and no problem status.

- Published INF: `oem5.inf`
- Installed version: `20.43.0.721`
- Installed SYS:
  `C:\Windows\System32\DriverStore\FileRepository\cueletvirtualaudio.inf_amd64_c4f370ba89a2550c\CueletVirtualAudio.sys`
- Installed SYS SHA-256:
  `B99FAB723EF9C0FF39CBC7CD0D34954C07D06341766D65FC64D5A529DCF902B3`
- Service: `cuelet_virtual_audio`, Running, Manual
- Root: `ROOT\CUELETVIRTUALAUDIO\0000`, OK, problem code 0
- Render: `Speakers (Cuelet Virtual Audio Device)`, OK
- Capture:
  `Cuelet Virtual Microphone (Cuelet Virtual Audio Device)`, OK

The installer reports `signatureTrusted=false` because its standard user-mode
WinVerifyTrust policy does not treat the local development root as a production
trust chain. The explicitly opted-in test-signing installation nevertheless
succeeded, the signed SYS and catalog passed the pre-install SignTool checks,
and Windows recorded no candidate Code Integrity failure.

The rollback command was printed and recorded before installation:

```powershell
& "$REPO\apps\windows\x64\Debug\Cuelet.VirtualAudio.Installer.exe" uninstall
```

## Stage B

All bounded lifecycle tests passed:

- 25 render open/close iterations;
- 25 capture open/close iterations;
- 25 independent start/stop iterations;
- 20 reset-during-activity iterations;
- 20 simultaneous render/capture iterations;
- 20 reader-recreation iterations;
- normal flow-test process shutdown;
- forced termination of only the active flow-test process, with zero remaining
  flow-test processes and both endpoints still healthy; and
- elevated devnode disable/re-enable.

During disable, the root became non-operational, endpoints became non-active,
and the service stopped. Re-enable restored the root, both endpoints, the
running service, the installed hash, and the endpoint pair. The WPP trace
showed teardown (`CVA201`, `CVA090`, and `CVA099`) followed by a clean new
`CVA100` through `CVA199` startup.

## Stage C

The 997 Hz, three-second render-to-capture test passed:

- 87.188 dB fitted SNR;
- peak 0.35004, no clipping;
- response -0.00032 dB;
- phase correlation 1.00000;
- no duplicate runs;
- no active zero-filled 1 ms quanta;
- no click candidates;
- one expected startup-boundary discontinuity marker;
- 85 ms onset;
- 5 ms bounded-duration drift;
- 198,720 captured frames and 144,000 aligned frames.

## Stage D

The requested 50, 80, 100, 200, 440, and 997 Hz bounded tones all passed. Each
had no clipping, no duplicate runs, no active zero-filled 1 ms quanta, and no
click candidates. SNR was approximately 87.18-87.23 dB. The prior
low-frequency distortion was not present.

The final broadband suite also passed silence, 40, 80, 100, 440, 997, 5,000,
and 12,000 Hz; a 20 Hz-20 kHz logarithmic sweep; multitone; impulse; pink
noise; a deterministic bass-heavy reference; and a speech-like reference.
The bass-heavy fixture achieved 81.618 dB SNR, 0.99946 phase correlation, no
clipping, no duplicate runs, no active zero quanta, no click candidates, and
zero duration drift. No periodic 1 ms gap pattern was observed.

The real-music pass played the installed Windows `Ring09.wav` through the
default Cuelet render endpoint and captured the Cuelet virtual microphone.
After normalizing both evidence files to 48 kHz stereo float:

- alignment correlation was 0.99999991;
- fitted SNR was 65.021 dB;
- fitted gain was 1.00000734;
- no samples clipped;
- 6,441 active source quanta contained zero missing 1 ms capture quanta;
- there were no duplicate-frame runs;
- nine one-second local alignment windows all had the same 47,360-frame lag;
- active-duration drift was 0 ms; and
- the source's measurable sub-200 Hz proportion (-7.4262 dB relative to
  full-band RMS) was preserved at a relative response of 0.0000 dB.

Two intermediate quality-suite assertions were harness defects, not driver
failures. Raw waveform slope mislabeled legitimate 5 kHz transitions as
clicks, so the detector now works on fitted-reference residuals with a
noise-relative threshold. The alignment search also stepped by four samples,
which aliases exactly at a 12 kHz/48 kHz period; the final search is
full-resolution. Both fixes were rebuilt in Debug and Release with warnings as
errors and the final broadband suite passed.

## Cleanup and system state

The installer removed the devnode, endpoints, package, catalog, DriverStore
directory, and SYS. As in the 20.42 cleanup, SetupAPI initially left the exact
stopped/disabled Cuelet service key marked `DriverDelete=1` and `DeleteFlag=1`
while referencing the already-removed tested DriverStore image. After its
ownership fields were verified, `DeleteService` returned success and removed
the key.

The installer has been hardened to perform that same final step itself. It
opens only `cuelet_virtual_audio`, verifies that its binary is the
`cueletvirtualaudio.inf_*` DriverStore `CueletVirtualAudio.sys`, and refuses to
delete a differently owned service. This post-runtime installer change builds
in Debug and Release with warnings as errors; the direct equivalent
`DeleteService` action was runtime-tested, but the newly rebuilt installer has
not yet been exercised around a newly installed package.

The final audit reports:

- no root devnode or phantom;
- no Cuelet audio endpoint;
- no service or service registry key;
- no Cuelet DriverStore package or installed SYS;
- no loaded Cuelet system driver;
- no flow-test process;
- C: `NOT Dirty`, Healthy, and OK;
- zero new NTFS/Disk/stornvme/storport/WHEA errors;
- zero bugcheck or live-kernel events;
- zero candidate Code Integrity errors; and
- zero Cuelet application errors.

## Files changed

Driver and instrumentation:

- `Cuelet.VirtualAudio.Driver/prepare-driver-source.ps1`
- `Cuelet.VirtualAudio.Driver/Cuelet.VirtualAudio.Driver.vcxproj`
- `Cuelet.VirtualAudio.Driver/overlay/EndpointsCommon/CueletAudioBridge.cpp`
- `Cuelet.VirtualAudio.Driver/overlay/EndpointsCommon/CueletStartupTrace.h`

Tests, runtime harness, and automation:

- `Cuelet.Core.Tests/CoreTests.cpp`
- `Cuelet.VirtualAudio.FlowTest/QualitySuite.cpp`
- `scripts/run-virtual-audio-stage-a.ps1`
- `scripts/complete-virtual-audio-stage-a.ps1`
- `scripts/cycle-virtual-audio-devnode.ps1`
- `scripts/finalize-virtual-audio-runtime.ps1`

Installer cleanup:

- `Cuelet.VirtualAudio.Shared/VirtualAudioIdentifiers.h`
- `Cuelet.VirtualAudio.Installer/main.cpp`

## Remaining risk and next step

The result covers bounded local runtime testing, not multi-hour stress,
aggressive repeated surprise removal, reboot persistence, or production
signing. The candidate is a Debug/test-signed build. Stage E remains the main
unmeasured risk area. The post-runtime installer deferred-service fix is
compiled and ownership-bounded but still needs an end-to-end installer
install/uninstall regression.

The exact next development step is to package the rebuilt hardened installer
around the unchanged immutable `20.43.0.721` candidate, perform one controlled
install/uninstall regression to prove the service key disappears without a
manual `sc delete`, re-audit the system, and report. If that is clean, begin
Stage E with bounded surprise-removal/reboot coverage before any multi-hour
stress run.
