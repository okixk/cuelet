# Cuelet Virtual Audio Driver Stage E and Release-Readiness Report

Date: 2026-07-24  
Candidate: `CueletVirtualAudio 20.43.0.721 Debug x64`  
Disposition: ready for continued daily development testing; not ready for
public release

## Outcome

The immutable `20.43.0.721` candidate passed the hardened-installer
regression, the two-hour changing-audio soak, the requested bounded lifecycle
categories, the shared-mode format matrix, Cuelet end-to-end application
integration, and three installed-driver reboot-persistence cycles. The final
supported uninstall and a separate cleanup reboot both left the machine free
of Cuelet driver residue.

No BSOD, unexpected restart, live-kernel report, pool-corruption indication,
NTFS Event 55, dirty filesystem, Disk/stornvme/storport/WHEA error,
candidate Code Integrity error, system-wide audio failure, or persistent
Cuelet PnP/audio error occurred.

Two requested coverage items remain incomplete:

- programmatic `SetSuspendState` requests did not cause this S0 Modern Standby
  laptop to enter a real sleep transition, so no sleep/resume cycle is claimed;
- OBS and Discord launched successfully, but the automated smoke did not prove
  capture from the Cuelet microphone inside those applications. Sound Recorder
  was not installed, and no unrelated software was added.

Those gaps do not block bounded daily development use. They do block a claim
that Stage E and public-release qualification are fully complete.

## Candidate identity

The candidate bundle remained unchanged:

`apps/windows/x64/Debug/DriverPackage-20.43.0.721-Candidate`

| Artifact | SHA-256 |
| --- | --- |
| `CueletVirtualAudio.sys` | `B99FAB723EF9C0FF39CBC7CD0D34954C07D06341766D65FC64D5A529DCF902B3` |
| `CueletVirtualAudio.pdb` | `7666EBDE2968644CB13B4FC0A2D5E3FA77DFE18C1BBD7BC17EB579E9FDB340A9` |
| `CueletVirtualAudio.inf` | `DC20D7DB9A9A9E3F524C060D2C83AE7128278B447707750AC45EA71A67C9D738` |
| `cueletvirtualaudio.cat` | `8481EA8F85787653C89E38065E9C0DBC352096662C86AB3A0804CF00FB7ACDE6` |
| `candidate-manifest.json` | `E80359D4C9A08E97E728E971B469B9D76EE7E8C410372C89056272A994FED3BA` |

During installation the package was `oem5.inf`. The installed binary was:

`C:\WINDOWS\system32\DriverStore\FileRepository\cueletvirtualaudio.inf_amd64_c4f370ba89a2550c\CueletVirtualAudio.sys`

Its measured SHA-256 was the locked SYS hash above in the installer
regression, soak, application tests, lifecycle categories, and all three
reboot cycles.

The original `20.42` Code 10 root cause remains precisely identified:
`IPortClsRuntimePower::QueryInterface` is optional, but its
`STATUS_NOINTERFACE` result was allowed to replace the successful functional
endpoint-installation status. `20.43` keeps the optional probe diagnostic
without returning it as the endpoint/start result.

## Installer regression

The rebuilt hardened installer passed a complete install/uninstall regression:

- all immutable candidate hashes matched;
- the expected OEM INF, DriverStore path, installed SYS hash, service, root
  devnode, two endpoints, and both endpoint relationships were verified;
- normal supported uninstall returned 0;
- deferred service deletion completed automatically;
- no `sc.exe delete` was used;
- package, service/key, devnode, endpoints, module, and installed SYS were
  absent after uninstall;
- C: was not dirty and remained Healthy/OK;
- no storage, candidate CI, or bugcheck/live-kernel event was found.

Evidence:
`apps/windows/x64/Debug/InstallerRegression-20.43.0.721-20260724-151238/installer-regression-result.json`

Five additional complete supported uninstall/install cycles passed during
Stage E. Each restored one valid endpoint pair and the exact locked SYS hash.
Two extra diagnostic cycles occurred while correcting the stress harness; both
rolled back or completed cleanly and are not counted in the requested five.

## Stage E duration and soak

The preserved Stage E evidence spans
`2026-07-24T15:17:10+02:00` through the final live audit at
`2026-07-24T19:26:11+02:00`, approximately 4 hours 9 minutes including
reboots, analysis, fixes, uninstall, and cleanup verification.

The continuous changing-fixture soak itself ran for 7,209.315 seconds
(2 hours 9.315 seconds):

| Metric | Result |
| --- | ---: |
| Soak cycles | 88 |
| Fixtures | 1,319 |
| Failed fixtures | 0 |
| Captured frames | 264,494,880 |
| Aligned frames | 183,108,720 |
| Clipped samples | 0 |
| Duplicate-frame runs | 0 |
| Zero-filled quanta | 0 |
| Click candidates | 0 |
| Excess position discontinuities | 0 |
| Inactive endpoint samples | 0 |
| Minimum SNR | 54.1754 dB |
| Minimum reference correlation | 0.99999809 |
| Maximum absolute duration drift | 5 ms |
| Maximum onset | 90 ms |

The changing fixture set exercised bass-heavy music, speech, silence
transitions, low-frequency tones, sweeps, intermittent playback, and bounded
start/stop periods.

Process working set changed from 32.46 MB to 33.63 MB, with a 37.30 MB peak.
Private bytes changed from 22.77 MB to 23.29 MB, with a 26.98 MB peak. Handle
count peaked at 179. PoolMon comparisons for Cuelet tags were flat or lower at
the end of the run; no monotonically growing Cuelet allocation was found.

Evidence:
`apps/windows/x64/Debug/StageE-20.43.0.721-20260724-151624/soak-2h/soak-summary.json`

The user-visible dropout indicators above are all zero. The Debug driver has
internal underflow and overflow counters, but `CVA409` did not export those two
counters into the WPP checkpoint payload. Consequently, exact cumulative
kernel underflow and overflow counts and continuous FIFO fill level are not
available from this run. No underflow/overflow manifestation was observed in
the captured output, but adding those values to a rate-limited diagnostic
checkpoint remains required before public qualification.

## Lifecycle stress

| Category | Completed | Result |
| --- | ---: | --- |
| Stream open/start/stop/close | 100 | Pass |
| Normal Cuelet process start/window-close | 25 | Pass |
| Forced Cuelet termination with active audio | 10 | Pass |
| Root devnode disable/enable | 20 | Pass |
| Windows Audio service restart | 10 | Pass |
| Complete supported uninstall/install | 5 | Pass |
| Installed-driver normal reboot | 3 | Pass |

The three reboot cycles used boot identifiers, pre/post state snapshots,
endpoint inventory, hash verification, event cursors, volume checks, and a
short 997 Hz end-to-end flow.

Cycle 1 initially used an overly short 0.5-second immediate-boot probe. It
failed during Audio Engine settlement, while the devnode, endpoints, service,
hash, events, and filesystem were already healthy. A delayed two-second
recovery probe passed with 87.183 dB SNR, correlation 1.0, zero zero-filled
quanta, and 5 ms drift. The harness was corrected without repeating the
reboot.

Cycle 2 booted at `2026-07-24T19:09:57.5000000+02:00` and passed on bounded
flow attempt 2. Cycle 3 booted at
`2026-07-24T19:11:53.5000000+02:00` and passed on attempt 3. The final passing
flow in each case had 87.183 dB SNR, correlation 1.0, response error
`-0.00048 dB`, zero clips/duplicates/zero quanta/clicks, and 5 ms drift.

The early retries show a real boot-settlement window in the Windows Audio
Engine, not a PnP failure. Production code and tests should wait for a
successful endpoint activation/flow rather than treating endpoint enumeration
alone as proof that the audio engine is ready.

The bounded reboot state ended at `completedCycles: 3`, removed its scheduled
task, and did not schedule a fourth installed-driver reboot.

## Sleep and resume

This machine exposes only `Standby (S0 Low Power Idle) Network Connected`.
Two bounded one-shot `SetSuspendState` attempts returned success but generated
only Kernel-Power event 187 (the API request). They produced no Modern Standby
entry/exit events 506/507.

The first version of the harness incorrectly treated endpoint health after the
API returned as a sleep/resume pass. That evidence is explicitly retired as a
false positive. The corrected harness waited through the one-shot wake window,
verified that the wake task ran, required an actual entry/exit event pair, and
correctly failed the attempt while confirming that Cuelet and C: remained
healthy.

Actual completed sleep/resume cycles: **0**.

A physical Start-menu, lid, or power-button Modern Standby test in the three
requested idle contexts remains the exact next platform test. It must not be
replaced with another loop around the ineffective API method. Microsoft
documents Modern Standby as a screen-off to screen-on user scenario:
<https://learn.microsoft.com/en-us/windows-hardware/design/device-experiences/modern-standby>.

## Formats and audio quality

All 12 shared-mode format cases passed with Windows Audio Engine conversion
enabled:

- 44.1 kHz and 48 kHz;
- mono and stereo;
- PCM16 and float32;
- render-side input and capture-side consumption.

Native 48 kHz stereo formats were accepted directly. Other cases correctly
used the shared-mode Audio Engine conversion path. Mono-to-stereo conversion
matched Windows' constant-power `-3.0103 dB` behavior.

Across the matrix:

- reference correlation was 1.0 in all cases;
- the minimum SNR was 62.4229 dB for converted 44.1 kHz capture;
- 48 kHz float paths reached 87.1712 dB;
- clips, duplicate runs, zero-filled quanta, and click candidates were all
  zero.

The strict direct capture comparison also passed at 80.18 dB SNR with
`0.999999995` correlation and no detected defects.

Evidence:
`apps/windows/x64/Debug/StageE-20.43.0.721-20260724-151624/format-matrix-corrected-2/format-matrix.json`

## Cuelet and real-application integration

The full Cuelet application integration passed:

- an unambiguous Cuelet-owned endpoint pair was automatically selected;
- saved third-party or explicit user endpoint choices were not overwritten;
- source playback reached the capture endpoint;
- bounded overlap followed Cuelet's mixing path without clipping;
- the configured physical-microphone route was observed;
- closing Cuelet quiesced the virtual output to exact silence.

The real-media comparison reported:

| Metric | Result |
| --- | ---: |
| 10 ms envelope correlation | 0.99436575 |
| Full-band gain | 0.63999928 (`-3.87641 dB`) |
| Bass gain | 0.63999900 |
| Bass-response error | `-0.00000377 dB` |
| Channel-balance error | `0.0000321 dB` |
| Peak | 0.157898 |
| Clipped samples | 0 |
| Zero-filled 10 ms quanta | 0 |
| Click candidates | 0 |
| Onset | 180 ms |
| Duration drift | 0 ms |

The gain matches the configured `0.8 × 0.8` media/application gain. No audible
bass loss, channel fault, click, gap, distortion, or accumulating overlap
latency was detected in the bounded sample.

Windows Sound Settings launched and showed the healthy Cuelet microphone
endpoint. OBS was already installed and initialized the Cuelet render endpoint
at 48 kHz. Discord was already installed and launched cleanly, but its new log
did not explicitly confirm Cuelet microphone capture. Sound Recorder was not
installed. No third-party program was installed for this test.

Therefore the Cuelet end-to-end path is proven, while a manual Sound Settings
meter check and explicit OBS/Discord microphone recording remain public-release
coverage tasks. An intentional default-device-change test also remains; the
app uses an explicit Cuelet endpoint ID, but that behavior was not separately
exercised by changing Windows defaults in this run.

## Kernel trace and lifecycle evidence

The Debug WPP trace decoded 115,041 checkpoint records:

| Checkpoint family | Count |
| --- | ---: |
| DriverEntry | 29 |
| Successful starts | 29 |
| Bridge epoch arms | 29 |
| Bridge teardowns | 56 |
| Stream initializations | 4,807 |
| Timer arms | 4,694 |
| Stream state exits | 28,616 |
| Functional failures | 0 |

The two expected cache misses per fresh endpoint install,
`CVA321/CVA323 STATUS_OBJECT_NAME_NOT_FOUND`, were initially misclassified as
functional failures. The classifier now recognizes only those known
pre-install cache misses as expected.

The ETL was 1.081 GB and decoded text was 22.0 MB. WPP is compiled out of the
Release driver (`#if DBG`), but the Debug ETL size is high enough that future
long soaks should use narrower/rate-limited keywords.

Evidence:
`apps/windows/x64/Debug/StageE-20.43.0.721-20260724-151624/stage-e-trace-summary.json`

## Defects found and targeted fixes

Driver SYS changes were not required; the immutable `20.43.0.721` candidate was
used throughout.

User-mode fixes:

- endpoint auto-selection now selects only one unambiguous Cuelet-owned
  render/capture pair when saved IDs are empty;
- installer `status` no longer enables physical-microphone mixing; only an
  explicit successful install may initialize that preference;
- forwarded `PlayId`, `PlayName`, `PlayFile`, and `Demo` activation commands
  are queued on the window dispatcher, avoiding
  `RPC_E_CANTCALLOUT_ININPUTSYNCCALL`;
- virtual-broadcast `MediaPlayer` objects use the `Other` audio category,
  while local monitor playback retains its normal category;
- bounded playback diagnostics were added.

Harness fixes:

- shared-mode format tests use Audio Engine auto-conversion and accept
  `IsFormatSupported` `S_FALSE` with the returned closest format;
- mono conversion checks use the expected constant-power gain;
- the media-player capture analyzer uses gain-independent envelope,
  bass-response, channel-balance, click, gap, clipping, onset, and duration
  checks;
- forced process tests use a valid PCM library fixture and wait for the async
  graph;
- admin stress retries only the known transient
  `AUDCLNT_E_DEVICE_INVALIDATED` settlement case;
- WPP classification excludes the two expected fresh-cache misses;
- sleep requires real power-transition evidence;
- reboot flow duration is two seconds with at most three bounded settlement
  attempts;
- final cleanup uses a one-shot boot task that removes itself.

Primary changed files:

- `apps/windows/Cuelet.WinUI/MainWindow.xaml.cpp`
- `apps/windows/Cuelet.VirtualAudio.FlowTest/QualitySuite.cpp`
- `apps/windows/Cuelet.VirtualAudio.FlowTest/QualitySuite.h`
- `apps/windows/Cuelet.VirtualAudio.FlowTest/main.cpp`
- `apps/windows/Directory.Build.props`
- `apps/windows/scripts/compare-virtual-audio-capture.py`
- `apps/windows/scripts/run-virtual-audio-stage-e-app-integration.ps1`
- `apps/windows/scripts/run-virtual-audio-stage-e-process-stress.ps1`
- `apps/windows/scripts/run-virtual-audio-stage-e-admin-stress.ps1`
- `apps/windows/scripts/run-virtual-audio-stage-e-sleep-cycle.ps1`
- `apps/windows/scripts/stop-virtual-audio-stage-e-trace.ps1`
- `apps/windows/scripts/verify-virtual-audio-stage-e-reboot.ps1`
- `apps/windows/scripts/run-virtual-audio-stage-e-reboot-sequence.ps1`
- `apps/windows/scripts/verify-virtual-audio-final-cleanup-reboot.ps1`

## Build and validation status

After the Stage E user-mode and harness fixes:

- full Debug x64 solution rebuild with warnings as errors: pass;
- full Release x64 solution rebuild with warnings as errors: pass;
- Debug and Release core tests: pass;
- fresh MSVC AddressSanitizer core-test build and run: pass;
- Python analyzer byte compilation: pass;
- PowerShell parser checks for the Stage E scripts: pass.

The unchanged SYS candidate retained its earlier completed driver validation:

- Debug and Release driver builds with `/WX`;
- WDK `/analyze`;
- InfVerif Windows and Universal modes;
- Inf2Cat;
- Universal ApiValidator;
- SignTool policy validation;
- symchk SYS/PDB matching;
- stale-SYS comparison;
- locked candidate hashes;
- retired-candidate selection regression.

The inherited SysVAD `/analyze` and InfVerif notices documented with the
candidate remain unchanged.

## Final uninstall, cleanup reboot, and live state

Supported uninstall returned 0 and reported `restartRequired: false`. Before
the final reboot it had already removed:

- OEM INF/package;
- service and service registry key;
- root devnode;
- both endpoints;
- loaded module;
- DriverStore SYS;
- Cuelet flow-test process.

The separate final cleanup reboot advanced the boot identifier from
`2026-07-24T19:11:53.5000000+02:00` to
`2026-07-24T19:20:42.5000000+02:00`. Its one-shot task passed and removed
itself.

The final live audit at `2026-07-24T19:26:11+02:00` found:

| Residue/safety check | Result |
| --- | --- |
| Cuelet scheduled tasks | 0 |
| Cuelet OEM INF matches | 0 |
| PnPUtil Cuelet package matches | 0 |
| Cuelet services | 0 |
| Cuelet service keys | 0 |
| Cuelet root devnodes | 0 |
| Cuelet audio endpoints | 0 |
| Loaded Cuelet modules | 0 |
| Cuelet DriverStore directories | 0 |
| Exact former installed SYS present | No |
| Cuelet MMDevice registry matches | 0 |
| Cuelet test processes | 0 |
| Filesystem dirty state | `Volume - C: is NOT Dirty` |
| C: health/operational state | Healthy / OK |
| New final-boot storage/WHEA/NTFS errors | 0 |
| New candidate Code Integrity errors | 0 |
| New bugcheck/live-kernel/unexpected-restart events | 0 |
| New Cuelet PnP/audio errors | 0 |

Evidence:

- `apps/windows/x64/Debug/StageE-20.43.0.721-20260724-151624/final-uninstall/final-cleanup-reboot-result.json`
- `apps/windows/x64/Debug/StageE-20.43.0.721-20260724-151624/final-uninstall/final-postboot-live-audit.json`

## Production-readiness review

The driver is not ready for public distribution.

Source/package reduction:

- 6 of 21 compile units are inherited device paths that Cuelet does not use:
  A2DP, Bluetooth HFP, USB headset, HDMI topology, mic-in topology, and S/PDIF
  topology;
- 46 of 70 INF sections are inherited HDMI/S/PDIF/speaker/mic-array/Bluetooth/
  USB/APO paths to review and remove;
- the production package should contain only the Cuelet render/capture pair and
  its required PortCls support.

Security and privacy:

- the inherited device SDDL grants generic access to Everyone and Restricted
  Code; it must be narrowed and threat-reviewed;
- physical microphone mixing must remain explicit and user-controlled;
- document that applications consuming the Cuelet capture endpoint can record
  audio routed by Cuelet;
- define behavior when Cuelet exits, permissions are revoked, or an upgrade
  fails;
- inherited endpoint `FxProperties` resembling the physical Realtek endpoint
  remain a package-cleanup risk and should not be relied on.

Identity and UX:

- the render endpoint still appears as
  `Speakers (Cuelet Virtual Audio Device)`;
- there is no intentional custom endpoint icon;
- provider, product naming, descriptions, icon, privacy warning, uninstall,
  and troubleshooting copy require a final UX/legal review.

Package behavior:

- the fresh Release SYS is 130,560 bytes and PDB is 1,495,040 bytes;
- Release WPP is compiled out;
- install/uninstall did not require a reboot in this run;
- upgrade from one supported Cuelet version to the next and failed-upgrade
  recovery remain untested because no second supported candidate exists;
- the adjacent application installer still contains retired
  `20.37.42.726` evidence. Selection policy correctly refuses it, but public
  packaging must replace/remove it and ship only the supported production
  package.

Qualification and signing:

- obtain the required real organizational signing certificate and Microsoft
  Hardware Developer Program access;
- produce a production submission and Microsoft-signed package;
- run the applicable HLK audio/device playlist on supported Windows releases
  and hardware;
- validate the returned signatures and symbols;
- test clean install, supported N-to-N+1 upgrade, rollback after a failed
  upgrade, uninstall, reboot persistence, and physical Modern Standby.

No production-signing attempt was made. Microsoft documents the Hardware
Dashboard/EV-certificate submission prerequisites and notes that attestation
signing is not a retail Windows Update publication path:
<https://learn.microsoft.com/en-us/windows-hardware/drivers/dashboard/code-signing-attestation>.
HLK qualification guidance is at
<https://learn.microsoft.com/en-us/windows-hardware/test/hlk/>, and Universal
Audio driver guidance is at
<https://learn.microsoft.com/en-us/windows-hardware/drivers/audio/audio-universal-drivers>.

## Readiness decision and next step

`20.43.0.721` is suitable for continued daily development testing under the
same bounded risk policy. It has strong evidence for start/stop, audio
fidelity, format conversion, process termination, devnode/audio-service
lifecycle, reinstall, reboot persistence, and complete supported cleanup.

It is not public-release ready. The next development step is to create a
source/package-minimization branch that removes the unused SysVAD surface,
narrows security, supplies final endpoint identity/icon metadata, exports
rate-limited FIFO underflow/overflow/fill diagnostics, and aligns the
application installer with the supported driver package. After that, create a
newly versioned candidate and run only the affected regression plus the
remaining physical Modern Standby, explicit OBS/Discord capture,
default-device-change, upgrade/rollback, HLK, and production-signing work.
