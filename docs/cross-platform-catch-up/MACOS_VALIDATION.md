# Cuelet macOS validation

Validation date: 2026-08-08. This record distinguishes automated tests,
source inspection, programmatic Core Audio evidence, measured audio evidence,
and visual evidence. All generated audio, captures, logs, and screenshots are
outside Git.

## Environment and baseline

| Item | Observed value |
|---|---|
| Hardware | MacBook Air M1 (2020) |
| OS | macOS 26.6, build 25G72 |
| Architecture | arm64 |
| CPU | 8 logical CPUs |
| Deployment target | macOS 14.0 |
| Installed driver | CueletVirtualAudio.driver, 0.1.5 build 6 (post-reboot) |
| Candidate | 0.1.7 build 8 diagnostic with ReadInput fix, uninstalled; SHA-256 `7e3d46ba0ef1d79c6d68cf36893f29dab391f88a3cfd17144300040c98569592` |
| Installed path | `/Library/Audio/Plug-Ins/HAL/CueletVirtualAudio.driver` |
| App bundle | current source expects 0.1.7 build 8 |
| Previous validation root | `/tmp/cuelet-driver-011-final-20260805-203737` |
| Timeline redesign root | `/tmp/cuelet-driver-timeline-redesign-20260806-070957` |
| Luna validation root | `/tmp/cuelet-driver-012-luna-validation-20260806-075517` |
| Sol fix evidence root | `/tmp/cuelet-driver-013-sol-fix-20260806-090702` |
| 0.1.3 post-reboot root | `/tmp/cuelet-driver-013-luna-postreboot-20260806-125216` |
| 0.1.4 property-failure evidence | `~/Documents/Cuelet-Validation/driver-014-live-debug` |
| 0.1.5 property-fix root | `/tmp/cuelet-diagnostic-property-fix-20260806-143822` |
| 0.1.5 live-counter root | `~/Documents/Cuelet-Validation/driver-015-live-counters` |
| 0.1.6 diagnostic root | `/tmp/cuelet-driver-016-sol-diagnostics-20260806-153456` |
| 0.1.7 read-fix root | `/tmp/cuelet-driver-017-sol-read-fix-20260808-105222` |

The worktree was intentionally dirty before this focused implementation. The
0.1.3 work changes only macOS driver/test/diagnostic/version integration and
the listed validation documents. Linux and Windows source remain unchanged.
The installed bundle was never overwritten.

## 0.1.3 Sol pre-install result

The exact all-zero branch is now source-reproduced. For the Luna source range
`[122552,123064)`, production `WriteMix` received nonzero 997/1499 Hz stereo
data with checksum `16609893262586320761`, approximately 0.25 peak and
0.1766/0.1768 RMS. In 0.1.2, `CueletResolveTimelineMapping` returned false
because the output operation did not carry a valid input timestamp. The
output timestamp itself was valid. The branch skipped `CueletRingWriteAt`, so
the later exact source read found 512 unpublished frames and returned silence.
Directly writing the same range into the same production ring returned all 512
frames and the original checksum.

Candidate 0.1.3 validates `mOutputTime` independently in `WriteMix`; read-side
origin mapping still uses a cycle with both timestamps. The recorded Luna
event replay now returns nonzero data with correct stereo bytes. Explicit
bounded read/write/timeline statuses expose the first rejected frame and
reason. Timestamp conversion rejects missing flags, negative, fractional,
non-finite, and overflowing values before signed frame arithmetic.

Pre-install validation passed: 1,222,189 core assertions, 136 contract
assertions, 50 recorded-event replay assertions, deterministic 305-second
44.1/48 kHz phase-continuity simulations, two-reader/start-order/generation/
wrap/marker/randomized tests, 100,000-iteration stress, ASan/UBSan, TSan,
Clang static analysis, Release bundle smoke/identity/signature verification,
and Swift Debug/Release builds with 107 tests, 2 opt-in skips, and no failures.
The candidate remains uninstalled. A manual full restart and repeated Luna
live transport test are required before the live defect can be marked fixed.

## Build and automated tests

The following completed successfully:

- `swift build` and `swift build -c release`;
- `swift test` and `swift test -c release`: 107 tests, 2 opt-in tests skipped,
  0 failures in each configuration;
- `CUELET_RUN_LIVE_AUDIO_TESTS=1 swift test -c release --filter
  AudioRoutingLiveIntegrationTests`: 1 passed;
- the Debug live audio integration test: 1 passed;
- the Release library performance test: 1 passed;
- driver timeline-core tests: 1,222,189 assertions;
- driver contract tests: 136 assertions;
- Luna recorded-event replay: 50 assertions;
- bundle smoke test and exact destination ownership guard;
- 100,000-iteration driver stress test;
- AddressSanitizer plus UndefinedBehaviorSanitizer driver-core test;
- ThreadSanitizer driver-core test; and
- Clang static analyzer on three driver translation units.

The installed 0.1.2 identity/hash were read without invoking the now-0.1.3
candidate verifier. The 0.1.3 Release candidate passed that verifier
independently. No reinstall or reboot was performed during this validation.

## Installed bundle and Core Audio discovery

The installed 0.1.2 build 3 executable remains
`c2e0387f689e57ef1a9ffed8c470c87c074cded0ae2e3caa54be55fa65bd54b3`.
The uninstalled 0.1.3 build 4 Release executable is
`44aeb0394f3eab81a6bad3592e0d6c5a9a74b717f959242612ead16521d72b48`.
The candidate passed `file`, `plutil`, bundle smoke, and strict deep
code-signature verification and remains ad-hoc with no Team ID. Core Audio is
still using the known-failing installed 0.1.2 bundle.

Independent discovery paths all found `Cuelet Virtual Microphone`:

- a Core Audio enumeration utility reported exactly one matching device with
  the expected name, manufacturer,
  stable UID/model UID, live device, input/output stream IDs, directions,
  channels, formats, rates, controls, and unknown-property behavior;
- Audio MIDI Setup showed 2 inputs and 2 outputs and exposed both input and
  output scope views;
- the direct HAL and `AudioDeviceIOProc` receivers opened both scopes; and
- the opt-in Swift live-routing test accepted System Output and the explicit
  Cuelet UID without changing the default output.

The Release GUI status surface was not conclusively exercised in isolation:
the app's fixed bundle identifier collided with an existing Cuelet process, so
no GUI Ready/selected claim is made from that launch.

Current live contract:

- device UID: `ch.oki.cuelet.virtual-microphone`;
- model UID: `ch.oki.cuelet.virtual-microphone.model`;
- input: `Cuelet Loopback Input`, stereo interleaved Float32;
- output: `Cuelet Injection Output`, stereo interleaved Float32;
- supported nominal rates: 44.1 and 48 kHz;
- device-scope input/output latency: 128 frames;
- device-scope input/output safety offset: 32 frames;
- stream latency: 0 frames; and
- zero-timestamp period: 16,384 frames; and
- functional volume and mute controls on input and output.

The device-level global latency/safety property query returned an unknown
property status, while the correct stream-scope queries returned 128/32. An
unknown property request returned `hasProperty=false` and an unknown-property
status. This is documented rather than masked by a code change.

The device was alive and the streams were active while idle. A direct HAL
receiver and injector were active during a programmatic inventory, but the
device-level running property remained 0. The direct receiver delivered
callbacks; explicit contract `StartIO`/`StopIO` tests pass. This is documented
as the observed host-controlled property semantics.

## Final Luna validation of installed 0.1.2

The corrected bundle was live-tested after the stated reboot. The result is a
transport failure, not a reduced-failure success: deterministic 997/1499 Hz
output was generated continuously by the direct Core Audio injector, but no
nonzero active samples were returned by either low-level receiver.

| Test | Delivered frames / duration | Audio result | Receiver timing |
|---|---:|---|---|
| 48 kHz, receiver before playback | 1,823,744 / 37.994667 s | entire capture zero | 3,562 callbacks; no jumps/gaps/size changes/errors |
| 48 kHz, receiver during playback | 863,744 / 17.994667 s | entire capture zero | 1,687 callbacks; no jumps/gaps/size changes/errors |
| 44.1 kHz, receiver before playback | 793,600 / 17.995465 s | entire capture zero | 1,550 callbacks; no jumps/gaps/size changes/errors |
| 44.1 kHz, receiver during playback | 617,472 / 14.001633 s | entire capture zero | 1,206 callbacks; no jumps/gaps/size changes/errors |
| 48 kHz, independent `AudioDeviceIOProc` receiver | 1,152,000 / 24.000000 s | entire capture zero | 2,250 callbacks; no timing errors |
| 48 kHz, two AUHAL receivers | 1,055,744 and 1,056,256 frames | both entire captures zero | both clients stable |
| 48 kHz, five-minute run | 14,735,872 / 306.997333 s | entire capture zero | 28,781 callbacks; no jumps/gaps/size changes/errors |

The five-minute injector produced 28,593 continuous 512-frame output
callbacks and 14,639,616 output frames. The receiver process RSS remained
approximately 59–60 MB and the injector approximately 130 MB; neither grew
without bound. There were no crashes, deadlocks, relevant Core Audio errors,
or `coreaudiod` restarts.

Because the active waveform never appeared, active zero-run counts and phase
jump counts are not meaningful as success metrics: the complete active section
was unavailable. Stereo identity, marker ordering, amplitude scaling, stale
audio after stop, and live mute/unmute sample effects therefore remain
unverified. The exact failure correlation is recorded in
`failure-correlation-012.txt`; the installed Release binary has diagnostics
disabled, so driver-side mapping-validity and ring counters are unavailable.

The device-level running property remained 0 during active receiver/injector
I/O while both stream `IsActive` properties remained 1. The direct receivers
continued receiving callbacks, and explicit StartIO/StopIO contract tests pass.

## Historical controlled signal transport (installed 0.1.1)

Fixtures were generated under the validation root at 44.1 and 48 kHz:

- Fixture A: 997 Hz left, 1,499 Hz right, leading and trailing silence;
- Fixture B: 440 Hz, silence, 660 Hz, silence, 1 kHz, silence;
- Fixture C: 0.75, 0.50, and 0.25 known levels; and
- Fixture D: 180-second stable signal with periodic markers.

The core contract is present: Cuelet output writes to the virtual output scope,
the driver ring receives the frames, and the virtual input scope is selectable
by a receiving client. Named AVFoundation captures at 44.1 and 48 kHz
contained the expected tones, correct stereo placement, approximately 0.25
peak and 0.1768 RMS in stable Fixture A active windows, and silence before and
after playback.

The following results are measured rather than inferred from a UI:

| Check | Result |
|---|---|
| 44.1 kHz | tones and stereo placement present; active peak 0.25; pre/post silence observed |
| 48 kHz | tones and stereo placement present; active peak 0.25; pre/post silence observed |
| 1499 Hz left/right isolated fixtures | clean channel-isolated captures |
| Fixture B markers | 440/660/1 kHz markers present; continuity not clean in the 1 kHz region |
| output volume 50% / 25% | peak 0.125 / 0.0625; RMS approximately 0.08836 / 0.04418 |
| input volume 50% / 25% | peak 0.125 / 0.0625; RMS approximately 0.08836 / 0.04418 |
| output/input mute | all captured samples zero in each isolated test |
| stale audio | stop/reset paths returned silence; core contract tests pass |
| clipping/DC | no clipping in controlled fixture windows; no material DC offset in long capture |
| approximate latency | no formal end-to-end number accepted; receiver timestamps are clean, but transport gaps make a latency claim invalid |

## Continuity investigation and installed 0.1.1 result

The installed post-reboot 0.1.1 driver reproduced phase/sample discontinuities in the
997/1499 Hz fixture. The valid input-only AUHAL receiver recorded 3,281 clean
512-frame callbacks during a 35-second run: no sample-time jumps, host-time
regressions, callback gaps, size changes, render errors, or bounded-transfer
drops. The native output injector recorded 2,812 continuous 512-frame output
callbacks. Nevertheless, the capture contained exact zero blocks between
otherwise exact fixture blocks. The 44.1 kHz control reproduced the same class
of zero-block interruptions with clean receiver timestamps. This excludes the
previous AVFoundation receiver and its timestamp/duration accounting as the
cause of the discontinuity.

The source-level cause is that the input reader consumed through the moving
producer head. HAL input and output operations are not required to publish in
the same callback order; the old reader could observe a partially published
cycle and return a mixed valid/zero window. `DoIOOperation` also ignored the
cycle boundary when choosing the readable ring head. Version 0.1.1's bounded
one-requested-buffer publication fence did not change those timeline
coordinates, so it could not provide continuity.

The installed 0.1.1 driver was tested after reboot. The one-buffer fence did
not remove the defect: at 48 kHz, a 37-second direct capture contained 37
active zero runs and 1,245/1,183 phase jumps for the 997/1,499 Hz channels;
the five-minute capture contained 399 active zero runs and 12,882/11,820 phase
jumps. The receiver reported no timestamp jumps, host regressions, callback
gaps, size changes, render errors, or bounded-transfer drops. The same failure
appeared at 44.1 kHz, in the `AudioDeviceIOProc` receiver, and with two
simultaneous receivers. Therefore 0.1.1 is not continuity-clean and no second
driver fix was applied speculatively.

The attempted publication fence remains covered by the guarded variable-frame
185-second simulation, client-slot guard reset coverage, contract tests, stress
tests, sanitizers, and static analysis. Installed 0.1.2 replaces that path with
an absolute `AudioServerPlugInIOCycleInfo` sample-time mapping and a generation-
tagged timeline ring. Its automated tests are clean, but live post-reboot
captures returned complete silence, so the redesign is not accepted.

The earlier approximately 146.472-second AVFoundation file-duration mismatch
did not reproduce in the valid direct AUHAL path: a direct 185-second capture
contained 8,880,128 frames (185.002667 seconds) with continuous timestamps.
That mismatch is therefore classified as an AVFoundation receiver/delivery
measurement limitation, not a confirmed driver clock-compression defect.

Two simultaneous named ffmpeg receivers both opened and received the expected
signal. They did not crash or corrupt each other, but their captures were not
accepted as a clean continuity proof. Rapid control changes, rapid open/close,
rate changes, reset, and process lifecycle scenarios remained stable.

## Timeline redesign implemented in installed 0.1.2

The old 0.1.1 correlation is now complete. The deterministic `WriteMix`
payload is clean at the source; the receiver has no timestamp or callback
errors; and the old `ReadInput` path is the first layer that can turn an
already-produced active range into a zero block. The installed Release binary
did not contain live diagnostic telemetry, so exact installed `mIOCycleCounter`
values are unavailable; the saved receiver/injector host timeline and source
branch decision are the evidence boundary.

The candidate stores output buffers at
`[mOutputTime.mSampleTime, mOutputTime.mSampleTime + actualFrames)` and maps
input through a fixed one-nominal-buffer delay plus the measured output-ahead
offset. On the stable 48 kHz host interval, output was 184 frames ahead of
input; the 512-frame chosen delay yields a 328-frame effective input-to-source
displacement. Validity is checked by absolute frame and reset generation, with
release/acquire publication and a post-read tag check. Multiple readers share
the timeline but maintain independent lifecycle state.

Automated and pre-install-equivalent evidence for 0.1.2:

- 1,020,637 timeline-core assertions, including seeded randomized event order;
- 185-second 44.1 and 48 kHz 997/1499 Hz phase-continuity simulations;
- two-reader, variable-size, wraparound, reset, missing-range, and genuine
  silence tests;
- test-only 4,096-frame conservative baseline;
- contract, bundle smoke, 100,000-iteration stress, ASan/UBSan, TSan, and
  static analyzer passes; and
- verified arm64 ad-hoc Release bundle 0.1.2 build 3.

The bundle is installed and was post-reboot live-tested. Its direct HAL and
independent `AudioDeviceIOProc` captures were entirely zero during active
playback, so the transport defect is not fixed. The installed Release binary
had diagnostics disabled, but the later exact production-interface replay
identified write-side mapping rejection: `WriteMix` skipped publication when
its operation lacked an input timestamp. Candidate 0.1.3 fixes that specific
branch and remains pending live post-reboot validation.

## Cuelet Release application boundary in the Luna pass

The fresh isolated Release app was built and a launch with disposable app
support and a demo library was attempted. Its fixed bundle identifier collided
with an existing Cuelet process, so the current GUI did not provide conclusive
Ready, version, selection, playback, pause/resume, Stop All, or route-change
evidence. Programmatic driver-service checks and the opt-in stable-UID routing
test remain valid.

The earlier isolated Release app discovered the previous installed device and
showed:

- `Ready`;
- expected, installed, and prepared version `0.1.1`;
- technical details for the expected bundle/device/model IDs; and
- `Ready — selected` after `Use as Cuelet Output`.

The selected stable UID persisted in the isolated settings JSON. Cuelet's live
integration test accepted System Output and the explicit installed UID without
changing the macOS default output. The settings UI explicitly explains that
virtual routing is one destination and does not mix a physical microphone or
play simultaneously through speakers.

The current installed bundle is 0.1.2 build 3. The opt-in live routing test
selected the explicit UID and System Output with a silent fixture and verified
that the system default output was unchanged; this does not prove active GUI
playback because the live transport capture was silent.

The Release app's sound cards combine their nested controls into one
accessibility element. UI automation could select the app state but could not
reliably invoke the nested play action. The app screenshot is therefore not
claimed as proof of GUI playback delivery. Named Core Audio captures and the
automated stable-UID routing test are the transport evidence.

## External application coverage

| Client | Result |
|---|---|
| Core Audio enumeration utility | device, scopes, formats, rates, controls, liveness observed |
| Audio MIDI Setup | device was observed; programmatic inventory is retained; visual captures were privacy-discarded |
| AVFoundation/ffmpeg named input | historical 0.1.1 signal/level/channel/mute/volume attempts; not accepted as 0.1.2 transport proof |
| Cuelet Release app | historical 0.1.1 Ready/version/technical/selected evidence; current 0.1.2 GUI state not conclusive |
| QuickTime Player | input selector showed Cuelet Virtual Microphone checked; no recording or active-signal claim was made because the driver transport was silent |
| Safari | not tested in this final driver session |
| Firefox | not tested |
| Microsoft Teams | not tested as a receiver; its unrelated installed audio device was not selected |
| Discord | not tested |
| OBS | not tested |

No universal receiving-app compatibility claim is made. No real ambient
microphone was recorded. The named virtual input was used for all accepted
captures.

## Sleep/wake

Sleep/wake was not automated because the session could not safely guarantee
survival of the capture, app, and evidence processes. It remains an exact
manual check: stop playback, sleep normally, wake, verify the UID/device and
unchanged defaults, replay a generated fixture, capture the virtual input, and
confirm no pre-sleep buffered audio.

## Evidence

The installed 0.1.2 Luna runtime evidence is under:

```text
/tmp/cuelet-driver-012-luna-validation-20260806-075517/
```

The 0.1.3 source reproduction, before/after checksums, exact event replay,
tests, sanitizers, analyzer, candidate verification, and pre-install manifest
are under:

```text
/tmp/cuelet-driver-013-sol-fix-20260806-090702/
```

The preserved 0.1.1 live evidence remains under
`/tmp/cuelet-driver-011-final-20260805-203737/`.

Each root has a `screenshots/manifest.csv`. The Luna root contains
generated fixtures and captures under `audio/`, filtered diagnostics and test
logs under `logs/`, and prior app/Core Audio/Audio MIDI Setup screenshots under
`screenshots/`. New direct-receiver evidence is primarily machine-readable
audio and JSONL telemetry; no private ambient microphone audio was captured.

## Remaining macOS limitations

- installed 0.1.2 live transport returns complete silence for deterministic
  output at both supported rates; candidate 0.1.3 fixes the source-reproduced
  branch but still requires install/reboot/live validation;
- installed-driver device-level running-state behavior remains only partially
  observed; explicit StartIO/StopIO contract behavior passes;
- live device-level running-state transition was not observed;
- physical-microphone mixing is not implemented;
- simultaneous speaker-plus-virtual output is not implemented;
- no sleep/wake pass in this session;
- no Safari, Firefox, Discord, Teams, or OBS receiver pass;
- arm64-only, ad-hoc, non-notarized development signing; and
- full VoiceOver, contrast, reduced-motion, drag-in/out, and broad UI coverage
  remain incomplete.

## 0.1.3 post-reboot Luna result — 2026-08-06

The installed post-reboot bundle is 0.1.3 build 4, arm64, and its executable
hash is `44aeb0394f3eab81a6bad3592e0d6c5a9a74b717f959242612ead16521d72b48`.
The installed bundle and current Release bundle have the same executable hash.
Core Audio enumerates exactly one Cuelet device with the expected stable IDs,
one two-channel input stream, one two-channel output stream, and 44.1/48 kHz
Float32 support. The built-in input and output defaults were unchanged.

The live transport fix is not confirmed. Generated 997/1499 Hz Float32 audio
was injected at both rates, but every direct capture remained completely zero:

- 48 kHz: 34.997333 s receiver-before-playback, 25.002667 s independent
  HAL-output-unit receiver, and 15.008 s producer-first captures;
- 44.1 kHz: 20.003991 s receiver-before-playback and 15.000091 s
  receiver-during-playback captures; and
- 48 kHz five-minute run: 14,736,384 frames / 307.008 s, all zero.

The AudioDeviceIOProc and HAL Output Audio Unit receivers agreed. Callback
timestamps were clean, with no callback gaps, size changes, render errors,
missing ranges, duplicate ranges, or block drops. This localizes the remaining
failure to the live driver output-publication-to-input-read path, but the
installed release did not expose enough live result telemetry to distinguish a
WriteMix publication miss from a ReadInput mapping/generation rejection.

The exact post-reboot evidence root is:

```text
/tmp/cuelet-driver-013-luna-postreboot-20260806-125216/
```

The focused Sol handoff is in `logs/sol-implementation-brief-013-postreboot.txt`.
The candidate remains unsuitable for acceptance until a diagnostic run proves
nonzero WriteMix publication and nonzero ReadInput delivery in the live path.

## 0.1.4 diagnostic pre-install scope

The 0.1.4 build is diagnostic-only. Source/SDK inspection confirms both live
operations are in-place on `ioMainBuffer`, that WriteMix is offered only for
the output stream and ReadInput only for the input stream, and that the driver
source has one static interface and shared loopback state. None of those facts
independently explains why the 0.1.3 installed path differs from the passing
production-interface replay, so transport behavior was not changed again.

The candidate adds bounded live evidence for every missing distinction:

- WriteMix call count, stream/client/cycle, all timestamp flags/bits/conversion
  statuses, selected buffer, bounded incoming peak/RMS/zero/checksum, resolved
  output range, accepted frames, generation, slots, and published tags;
- a separate post-volume/mute payload summary, so valid incoming output cannot
  be confused with intentional driver-side muting;
- ReadInput mapping and source range, valid/zero-filled frames, first rejection
  code/frame, expected and observed tag/generation, and returned payload;
- opaque driver-state and ring tokens on every event;
- initialization, StartIO/StopIO counts, stream activation, sample-rate changes,
  reset count, and generation transitions; and
- explicit early-return disposition for missing main buffer, wrong stream,
  inactive stream, missing client reader, and unsupported operation.

Telemetry is exported through private Core Audio device properties and decoded
outside the callback by `cuelet-driver-diagnostics`. Callback work is bounded:
8,192 events, no allocation or blocking, and at most 8,192 stereo frames
inspected per operation. The workflow script verifies the exact installed
0.1.4 hash, records only generated virtual-device audio, and writes its output
under `/tmp`:

```bash
./apps/macos/scripts/run-virtual-audio-live-diagnostics.sh
```

Installation and a manual restart remain required before that command can
observe live 0.1.4 events. The installed 0.1.3 bundle was not replaced during
candidate construction.

Pre-install evidence passed 1,222,189 core assertions, 237 interface contract
assertions, 50 exact Luna replay assertions, 551 diagnostic store assertions,
the 100,000-iteration stress path, ASan/UBSan, TSan, and a clean Clang static
analyzer rerun. Swift Debug and Release each passed 107 tests with two opt-in
skips. The Release diagnostic bundle is thin arm64, ad-hoc signed, 89,424
bytes, version 0.1.4 build 5, with executable SHA-256
`cf8896993fcbe6e34d86147a5d2bf6f16c14d33021ec5e17241a1520dcea818e`.
Evidence is under:

```text
/tmp/cuelet-driver-014-diagnostic-sol-20260806-140202/
```

## 0.1.4 live property failure and 0.1.5 correction

After 0.1.4 installation and restart, the inspector's `status`, `snapshot`, and
`summarize` commands all failed before audio testing with “Cuelet diagnostic
properties are unavailable.” Direct reproduction returned status 1; `selftest`
remained successful because it is synthetic and does not contact Core Audio.
No diagnostic clear occurred.

The property probe resolved bundle ID
`ch.oki.cuelet.virtual-microphone.driver` to plug-in object 33 and stable UID
`ch.oki.cuelet.virtual-microphone` to public device object 92, input stream 94,
output stream 93, and controls 95–99.
Across the Cuelet hierarchy, global/input/output/wildcard scopes, and
main/master-alias/wildcard elements, `cust`, `cdsv`, `cdct`, `cdev`, `cdec`,
`cdcl`, `cdbv`, and `cden` were all absent. Size/get/settable queries returned
`kAudioHardwareUnknownPropertyError` (`'who?'`). The inspector's original
device/global/main address and all FourCC integers matched the driver header.
The installed and then-current Release executable were byte-identical at hash
`cf8896993fcbe6e34d86147a5d2bf6f16c14d33021ec5e17241a1520dcea818e`.

Apple's active macOS 26 SDK defines `cust` as the array of
`AudioServerPlugInCustomPropertyInfo` entries the host uses to marshal custom
properties. It permits only `CFString` and `CFPropertyList` custom data. The
0.1.4 driver omitted `cust` and returned raw structures, so coreaudiod did not
publish those selectors to clients. This is a driver property-dispatch defect,
not an inspector-side object lookup failure.

Candidate 0.1.5 build 6 adds only the required device/global/main metadata and
CFPropertyList boundary. The seven selectors use `CFData`, `CFNumber`, or
`CFBoolean`; no transport, ring, I/O callback, format, or public identity code
changed. The inspector now includes `probe-properties` and actionable
selector-specific errors. The workflow retains partial evidence and writes a
stage-specific `workflow-error-summary.txt` before returning failure.

Pre-install validation passed 1,222,189 core assertions, 338 diagnostic
interface assertions, 136 diagnostics-disabled interface assertions, 50 Luna
replay assertions, 551 diagnostic-store assertions, 100,000 stress iterations,
ASan/UBSan, TSan, Clang static analysis, Release bundle property smoke, and
Swift Debug/Release (107 tests, two opt-in skips, zero failures each). The
candidate is thin arm64, ad-hoc signed, 90,288 bytes, and has executable SHA-256
`6436b80d147646a610416614c453ef573166b374ca65e6990d8fcd34a2372404`.
Evidence is under:

```text
/tmp/cuelet-diagnostic-property-fix-20260806-143822/
```

At the end of that property-access task, 0.1.4 remained installed and candidate
0.1.5 still required explicit installation and a manual full restart. The
subsequent 0.1.5 live result is recorded below.

```bash
./apps/macos/scripts/run-virtual-audio-live-diagnostics.sh
```

Transport remains unresolved; this change only prepares access to live
diagnostic evidence.

## 0.1.5 live counters and 0.1.6 diagnostic candidate

The post-reboot 0.1.5 workflow reached real injector and receiver I/O even
though `cdev` event streaming failed. Its final counters measured 1,313
`WriteMix` calls (845 nonzero and 468 zero), 672,256 requested and accepted
write frames, and zero rejected write frames. It measured 1,313 `ReadInput`
calls, zero valid frames, and 672,256 zero-filled frames. There were two ring
resets, two generation changes, one StartIO and one StopIO. State token
`2ebdb30ac10a0c62` and ring token `5cd4dd6dec7e6309` were stable. The 48 kHz
injector delivered 480,256 frames over 10.005333 seconds with no unsupported
buffers; the receiver delivered and wrote 672,256 frames over 14.005333
seconds with no queue/event drops, but its WAV was silent.

The public `cdev` address was present on device 92 at global scope/main
element. `AudioObjectGetPropertyDataSize` returned four bytes and
`AudioObjectGetPropertyData` returned `!siz`; the other six private properties
returned contract-correct CF property-list references. Version 0.1.6 replaces
the monolithic event export with exact-length immutable `CFData` pages of at
most 256 events, using a typed CFData cursor qualifier. This keeps the outer
custom-property representation as one retained `CFPropertyListRef`, validates
type/length/schema before decoding, and permits incremental sequence-checked
polling.

The working `cdct` property is extended independently. It distinguishes write
input, validated, stored-payload, and release-published-tag frames and records
publication failures. It classifies every unavailable read frame and preserves
first/last failure metadata, last accepted/published write ranges, last
resolved read range, and 64 critical startup events. The live workflow now
continues through injector, receiver, final counters, WAV analysis, and both
summaries when optional event streaming fails; it reports the event failure
only after preserving all fallback evidence.

A read-only follow-up using the new backwards-compatible inspector against the
still-installed 0.1.5 driver reported 657 `WRITE_OK` calls and 657
`READ_TIMELINE_UNINITIALIZED` calls. The associated timeline counters were 657
`TIMELINE_OK` and 657 `TIMELINE_OUTPUT_INVALID`. Source correlation identifies
the exact current boundary: live `ReadInput` supplies a valid `mInputTime` but
no valid operation-local `mOutputTime`; `CueletResolveTimelineMapping` returns
before `CueletRingReadAt`. Candidate 0.1.6 exposes that as
`READ_MAPPING_INVALID` with flags and ranges. It deliberately does not alter
the mapping because a correct cross-operation origin has not yet been measured
from a working event stream.

Pre-install validation passed 1,222,189 core assertions, 9,937 diagnostic
interface assertions, 136 diagnostics-disabled assertions, 50 Luna replay
assertions, 1,123 telemetry-store assertions, 100,000 stress iterations,
ASan/UBSan, standalone UBSan, TSan, static analysis, Release bundle smoke, and
Swift Debug/Release (107 tests, two opt-in skips, no failures each). Workflow
success and simulated-event-failure dry runs both retained the required
summaries. Candidate 0.1.6 is thin arm64, ad-hoc signed, 91,024 bytes, and has
SHA-256 `9420cd08fb5c38dd30514b80fb14eecabc9a2d61cdd17f6775a6596d8055cb54`.
It was not installed. A manual restart is required after any approved install,
then the exact command is:

From the repository root:

```bash
./apps/macos/scripts/run-virtual-audio-live-diagnostics.sh
```

No transport repair is claimed.

## 0.1.7 ReadInput timeline correction — pre-install

The retained 0.1.5 read-only query counted 657 `WRITE_OK`, 657
`READ_TIMELINE_UNINITIALIZED`, and 657 `TIMELINE_OUTPUT_INVALID` results. Every
read had a valid input timestamp but no valid operation-local output timestamp.
The production resolver returned at that output validation branch, so
`CueletRingReadAt` was never reached despite 672,256 write frames having been
reported accepted in the preceding live run.

The macOS 26.5 SDK describes `mInputTime` and `mOutputTime` as the independent
input and output data starts for a common I/O cycle. It describes the cycle
counter as the shared ordinal and notes that actual operation frame counts can
differ from nominal. Version 0.1.7 therefore records input/output observations
separately and calibrates only when cycle counter, ring generation, and sample
rate match. Later ReadInput calls resolve from valid input time plus the stored
measured offset and intentional one-buffer delay; they do not require an
operation-local output timestamp.

The exact production-interface regression failed before the change with
`TIMELINE_OUTPUT_INVALID`, zero valid frames, and a zeroed destination. After
the change the same input-valid/output-invalid call reached ring lookup,
returned 512 valid deterministic stereo frames, and preserved the source
checksum. Receiver-first startup returns one deliberate uncalibrated silent
block, a matching output-only cycle establishes calibration, and the next
input-only block succeeds at both 44.1 and 48 kHz. The full timestamp matrix
rejects invalid input time without fabricating a position. Output-first,
additional-reader, producer-restart, and unequal 384/128/256 callback tests
also pass.

Calibration is invalidated on final StopIO, sample-rate change, and explicit
timeline reset. Intermediate client starts/stops do not reset it. The
implementation uses fixed-size atomics, release/acquire publication, and
bounded retry counts; no callback allocation, blocking, sleep, logging, file
I/O, or IPC was added. Candidate 0.1.7 build 8 is thin arm64, ad-hoc signed,
91,360 bytes, and has executable SHA-256
`7e3d46ba0ef1d79c6d68cf36893f29dab391f88a3cfd17144300040c98569592`.
It remains uninstalled. Live nonzero transport and continuity require an
explicit install, manual full restart, and the existing diagnostic workflow;
they are not claimed by this source-level validation.
