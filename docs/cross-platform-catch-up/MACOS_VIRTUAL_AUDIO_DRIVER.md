# Cuelet Virtual Microphone for macOS

Status: the installed 0.1.5 build 6 diagnostic driver is loaded by Core Audio
but returns all-zero input. Its counters prove successful nonzero WriteMix
calls followed by 657 ReadInput failures caused by an invalid operation-local
output timestamp. The uninstalled 0.1.7 build 8 candidate keeps the 0.1.6
diagnostics and fixes that focused read-resolution dependency. Live transport
and continuity remain unconfirmed until post-reboot validation. The driver is
not a production artifact.

## Identity and installation

| Item | Observed value |
|---|---|
| Public device name | `Cuelet Virtual Microphone` |
| Manufacturer | `Cuelet` |
| Bundle | `/Library/Audio/Plug-Ins/HAL/CueletVirtualAudio.driver` |
| Bundle identifier | `ch.oki.cuelet.virtual-microphone.driver` |
| Device UID | `ch.oki.cuelet.virtual-microphone` |
| Model UID | `ch.oki.cuelet.virtual-microphone.model` |
| Installed version/build | `0.1.5` / `6` diagnostic |
| Installed executable SHA-256 | `6436b80d147646a610416614c453ef573166b374ca65e6990d8fcd34a2372404` |
| Candidate version/build | `0.1.7` / `8` diagnostic with ReadInput fix (not installed) |
| Candidate executable SHA-256 | `7e3d46ba0ef1d79c6d68cf36893f29dab391f88a3cfd17144300040c98569592` |
| Architecture | thin `arm64` |
| Deployment target | macOS 14.0 or later |
| Signature | valid ad-hoc signature; no Team ID |
| Current host | macOS 26.6 build 25G72, M1 arm64 |

The installed 0.1.5 executable remains
`6436b80d147646a610416614c453ef573166b374ca65e6990d8fcd34a2372404`.
The 0.1.7 Release candidate is verified separately as a thin arm64, ad-hoc
signed diagnostic bundle. Its read-timeline behavior requires explicit
installation and a manual restart before live Core Audio transport can be
confirmed.

Validation did not reinstall, replace, uninstall, or reboot the driver. The
existing post-reboot installation was observed in place.

## 0.1.3 source-level all-zero fix

The exact Luna sequence was replayed through the production driver interface.
The 0.1.2 `WriteMix` branch received nonzero 997/1499 Hz stereo data for
`[122552,123064)`—peak approximately 0.25 per channel, RMS approximately
0.1766/0.1768, checksum `16609893262586320761`—but did not call the ring
writer. `CueletResolveTimelineMapping` required both the output and input
sample-time flags; the live output operation had a valid `mOutputTime` and no
valid `mInputTime`. The rejected write left the exact later source request
unpublished, so `ReadInput` returned 512 zero frames. A direct ring write/read
of the same range and generation returned all 512 original frames, excluding
the ring metadata and payload storage as the cause of this failure.

The 0.1.3 candidate makes `WriteMix` convert and validate only its authoritative
output timestamp. Input/output origin calibration remains read-side work for a
cycle with both timestamps. Explicit bounded diagnostic statuses now expose
invalid timestamp, generation, publication, absolute-frame, overwrite,
not-yet-written, and partial-range results without callback logging or
allocation. Startup without historical output remains deterministic silence;
additional readers and intermediate client stops do not reset the shared ring;
the first start/final stop and sample-rate reset advance the generation.

The exact recorded Luna replay, 1,222,189 core assertions, 136 contract
assertions, 50 replay assertions, 305-second simulations at 44.1 and 48 kHz,
two-reader/start-order/reset/wrap tests, 997/1499 Hz phase tests, marker order,
seeded randomized ordering, 100,000-iteration stress, ASan/UBSan, TSan, static
analysis, and bundle verification all pass. These are source-level and
automated pre-install results. Only a later post-reboot live pass can establish
that 0.1.3 transports real Core Audio audio.

## Architecture decision

Cuelet uses a standard-C Audio Server Driver Plug-in, not AudioDriverKit, a
DriverKit system extension, a kernel extension, or the deprecated kernel audio
driver model. The plug-in has no Cuelet application dependency, file access,
network access, IPC, UI, or physical-microphone capture path. It runs in the
Core Audio server process and publishes one virtual device with paired input
and output scopes.

The plug-in follows Apple's Audio Server Plug-in object/property model. Cuelet
keeps privileged installation in explicit Terminal scripts, requires a
manual restart after installation or removal, never changes system defaults,
never disables SIP, never kills `coreaudiod`, and never touches another
vendor's bundle.

## Core Audio contract

The live device exposes:

- one input stream, `Cuelet Loopback Input`;
- one output stream, `Cuelet Injection Output`;
- two channels in each direction;
- interleaved 32-bit Float32 linear PCM, 8 bytes per frame;
- explicit nominal sample-rate options at 44,100 and 48,000 Hz;
- current rate verified at both 44.1 kHz and 48 kHz;
- 128-frame device-scope input/output latency and 32-frame safety offset;
- 0-frame stream latency;
- a 16,384-frame zero-timestamp period; and
- functional input/output master volume and mute controls.

The independent Core Audio inventory found the expected name, manufacturer,
UID, model UID, input/output scopes, stream directions, channel counts,
formats, rates, controls, and liveness. Audio MIDI Setup independently showed
the same device with 2 inputs and 2 outputs. The device was also discovered by
AVFoundation and Cuelet's driver service.

The global-scope latency and safety-offset query returned Core Audio's unknown
property status on this host; the stream-scope queries returned 128 and 32
frames. This is recorded as a property-scope limitation, not as a fabricated
global value. Unknown property requests returned `hasProperty=false` and an
unknown-property status. Unsupported format/rate/property paths are covered by
the contract tests and return errors rather than fabricated values.

The idle device reports alive and its streams report active. During an active
direct HAL receiver/injector inventory, the device-level running property still
reported 0. The receiver delivered callbacks and the contract test's explicit
`StartIO`/`StopIO` path passes; this property is recorded as host-controlled
on this driver model rather than animated artificially.

## Audio graph and ring policy

```text
Cuelet AVAudioPlayer / output client
        |
        v
Cuelet Injection Output (WriteMix)
        |
absolute `mOutputTime.mSampleTime` range
        |
preallocated 16,384-frame timeline ring
        |
Cuelet Loopback Input (ReadInput)
        |
AVFoundation / Core Audio receiving client
```

The installed 0.1.2 implementation stores each stereo frame under its absolute output sample
time. A slot is valid only when its absolute frame and reset generation match;
the stereo payload is one atomically published 64-bit value. `ReadInput`
maps its input sample-time range through a fixed one-nominal-buffer delay and
the measured output-ahead offset. It never follows a producer head, jumps a
slow reader to a newest window, or treats an arbitrary slot payload as the
requested frame. Unwritten, overwritten, and previous-generation ranges are
returned as bounded silence and counted as unavailable/stale.

The first I/O start, final I/O stop, explicit reset, and sample-rate changes
advance the timeline generation. Each reader has independent lifecycle state
but no independent moving audio cursor; multiple readers can request the same
absolute range without corrupting one another. Publication stores payload and
generation before releasing the absolute-frame tag; readers acquire and
recheck the tag after loading the payload. No allocation, blocking
synchronization, logging, file access, Swift/Objective-C call, or HAL client
call occurs in the real-time I/O operation.

## Timeline redesign implemented in 0.1.2

The 0.1.1 failure correlation showed clean deterministic `WriteMix` input and
clean receiver timestamps, while the old moving-head reader returned zero
windows whenever input arrival briefly outran the independently advancing
publication counter. The saved 48 kHz run contains active zero runs at input
frames 128,512, 130,048, and 131,072; the nearest output callbacks had already
provided the corresponding host-time region. The installed Release binary had
diagnostics disabled, so its `mIOCycleCounter` and driver-side validity reason
were not available retrospectively.

The installed implementation uses the Core Audio cycle contract directly:

```text
WriteMix:   [mOutputTime.sample, mOutputTime.sample + actualFrames)
            -> absolute timeline ring
ReadInput:  [mInputTime.sample, mInputTime.sample + actualFrames)
            -> input + fixed (output-ahead offset) - one nominal buffer
```

The host-timeline baseline measured the output timestamp 184
frames ahead of the input timestamp in the stable 48 kHz region. With the
observed 512-frame nominal buffer, the implementation therefore reads the prior
output window using an effective input-to-source displacement of 328 frames.
The mapping is established once per timeline generation and is not chased by a
moving producer position. Variable actual buffer sizes are accepted by the
absolute-range storage; the fixed delay is deliberately conservative for the
current host contract.

The deterministic reference/model suite covers read-before-write, irregular
ordering, variable and split ranges, two readers, ring wrap, reset generation,
missing ranges, genuine silence, 44.1/48 kHz phase-continuous 997/1499 Hz
signals, five-minute simulated timelines, and a seeded event-order model. A
test-only 4,096-frame conservative baseline is also clean. The redesigned
transport is now loaded by Core Audio; its live result is recorded above. The
live captures returned complete silence, so the automated model and bundle
evidence do not establish correctness.

## Historical runtime results (installed 0.1.1)

The controlled generated fixtures were stored under
`/tmp/cuelet-macos-driver-continuity-20260804-204602` and were never added to
Git:

- Fixture A: 997 Hz left / 1,499 Hz right, with leading/trailing silence;
- Fixture B: 440 Hz, silence, 660 Hz, silence, 1 kHz, silence;
- Fixture C: known 0.75/0.50/0.25 levels;
- Fixture D: 180-second stable signal with periodic markers.

The historical installed 0.1.1 driver delivered the expected 997 Hz left and 1499 Hz
right tones at both 44.1 and 48 kHz, with approximately 0.25 peak and
0.1768 RMS in active windows, leading/trailing silence, volume/mute behavior,
and stable process lifetimes. It was not continuity-clean: a native output
injector produced continuous 512-frame callbacks while a direct input-only
AUHAL receiver recorded clean timestamps but exact zero-filled gaps between
active blocks. The same class of gap appeared at 44.1 kHz.

The direct receiver is now a bounded, callback-safe diagnostic tool using the
HAL Output Audio Unit input bus and `AudioUnitRender`. It records callback
sequence, requested/actual frames, sample time, host time, flags, size
changes, jumps, gaps, render errors, and bounded-transfer drops outside the
real-time callback. In the 35-second 48 kHz installed-driver reproduction it
recorded 3,281 callbacks with no timestamp/callback/transfer errors; the
transport still contained zero blocks. This localizes the failure to the
installed driver’s publication/read boundary rather than to AVFoundation.

The installed 0.1.1 adds a one-requested-buffer publication fence per reader,
retains the largest observed guard across callback-size changes, and rejects
partial guarded windows. Its deterministic 185-second variable-frame,
two-reader, non-bin-aligned waveform simulation is clean. Post-reboot live
testing disproved this as a complete fix: direct HAL captures still contain
active zero blocks and phase jumps at both supported rates, including with two
readers and a five-minute run. Receiver sample/host timestamps remain clean.
The later 0.1.2 redesign correlates `AudioServerPlugInIOCycleInfo` sample
ranges with the ring publication timeline. Its post-reboot live result is
recorded in the current section below and is not transport-clean.

The earlier approximately 146.472-second AVFoundation file-duration mismatch
did not reproduce in a valid direct receiver: a direct 185-second capture
contained 8,880,128 frames (185.002667 seconds) with continuous timestamps.
It is therefore recorded as an AVFoundation delivery/measurement limitation,
not as a confirmed driver clock-compression defect.

## Final Luna runtime result (installed 0.1.2)

After reboot, the installed 0.1.2 driver enumerated correctly and accepted
direct output and input clients, but the loopback transport returned complete
silence. At 48 kHz, a 37.994667-second AUHAL capture, a 17.994667-second
AUHAL capture started during playback, a 24-second `AudioDeviceIOProc`
capture, two simultaneous AUHAL captures, and a 306.997333-second capture all
contained zero samples throughout. At 44.1 kHz, 17.995465-second and
14.001633-second captures had the same result.

The injectors produced continuous 512-frame output callbacks. Receivers
reported no sample-time jumps, host-time regressions, callback gaps, callback
size changes, render errors, or bounded-queue drops. The five-minute run had
28,781 callbacks and 14,735,872 delivered frames. The two receiver processes
remained stable and resource usage did not grow without bound.

The host-timeline correlation measured output sample time exactly 184 frames
ahead of input, with 32,000 host ticks. Under the candidate mapping, source
ranges such as `[122552,123064)` and `[1121976,1122488)` were within the
continuous output timeline and expected active fixture region, but the input
capture remained zero. The installed Release binary has diagnostics disabled,
so the final internal branch—invalid mapping, rejected write, or unavailable
ring read—cannot be distinguished retrospectively. The demonstrated defect is
that valid host-visible output never becomes readable loopback audio.

The device-level running property remained 0 while stream `IsActive` remained
1 and direct callbacks continued. Volume/mute property writes and readback
worked at 1.0, 0.5, 0.25, mute, and unmute, but sample-level scaling could not
be validated because transport was already silent.

## Historical volume and mute result (installed 0.1.1)

The advertised controls affect samples, not just UI state:

| Operation | Measured result |
|---|---|
| output volume 1.0 | baseline peak 0.25 |
| output volume 0.5 | peak 0.125, RMS approximately 0.08836 |
| output volume 0.25 | peak 0.0625, RMS approximately 0.04418 |
| input volume 0.5 | peak 0.125, RMS approximately 0.08836 |
| input volume 0.25 | peak 0.0625, RMS approximately 0.04418 |
| output mute | captured samples were all zero |
| input mute | captured samples were all zero |

Rapid mute/unmute and full/quarter-level changes during active playback were
applied without a crash or deadlock. Controls were reset to volume 1.0 and
mute off at the end of validation. Independent property behavior is correct in
the contract tests and in the live amplitude captures.

## Historical client and lifecycle coverage (installed 0.1.1)

The following live scenarios were exercised with generated signals and named
Cuelet input selection: input before output, output without a receiver, one
receiver, two simultaneous receivers, receivers joining during playback,
repeated receiver open/close, rapid play/stop and control changes, sample-rate
changes while idle and around playback, reset after previous audio, and
long-running playback. Core contract/stress tests additionally cover reader
overrun, many ring wraps, independent cursors, start/stop, pause-like gaps,
stale-audio prevention, and bounded reader state.

Two simultaneous named receivers both opened and received the expected
signal, and the direct AUHAL receiver itself reported no bounded-transfer
drops. Their pre-fix captures were not used as a clean continuity proof
because the installed driver's reader-boundary defect remained. The direct
AUHAL receiver is accepted as the lower-level timestamp baseline; the
AVFoundation duration mismatch is not used as a driver-clock result.

## Final 0.1.2 client and lifecycle result

The installed 0.1.2 driver accepted one and two direct receivers, receivers
opened before or during injection, repeated receiver open/close cycles, and
100 direct output start/stop cycles without crashes, deadlocks, callback
errors, or process instability. Both simultaneous receiver captures were
entirely zero, however, so they prove client isolation and callback stability
only—not audio transport correctness. The five-minute receiver and injector
also remained resource-stable.

The direct receiver returned to zero after stop and restart, but because it was
already zero during active playback this is not a positive stale-audio result.
Cuelet GUI play/pause/resume/Stop All and route-away/route-back actions were
not conclusively exercised in isolation because the fixed app bundle
identifier collided with an existing Cuelet process. The opt-in Swift routing
test did select the explicit UID and System Output and verified unchanged
system defaults using a silent fixture.

## Cuelet application integration (current validation boundary)

The previous isolated Release app found the earlier installed 0.1.1 device and
displayed:

- `Ready` status;
- expected, installed, and prepared version `0.1.1`;
- the stable bundle/device/model IDs in Technical details; and
- `Ready — selected` after `Use as Cuelet Output`.

The selected `coreaudio:ch.oki.cuelet.virtual-microphone` value persisted in
isolated app settings. The automated Debug and Release live integration test
selected System Output and the installed UID and verified that macOS defaults
were unchanged. Cuelet's service refuses file-only readiness and requires both
live stream directions.

The current 0.1.2 installation was present for this pass. A fresh isolated
Release launch was attempted with disposable demo data, but the app's fixed
bundle identifier collided with an existing Cuelet process. Therefore no
conclusive current GUI Ready/version/selected-state or GUI playback claim is
made. Programmatic driver-service and Core Audio evidence remains valid.

The current application source expects 0.1.3 build 4; while 0.1.2 remains
installed it correctly treats the installed bundle as an older updateable
driver. Candidate 0.1.3 integration is automated-test verified, not GUI-runtime
verified.

The accessibility-combined sound-card surface prevented a reliable automated
click of the nested play button in this session. Therefore the Release GUI
screenshots are evidence of real device discovery and selection, not a claim
that a GUI card click delivered the generated fixture. Signal delivery is
proven separately by the named Core Audio receiver captures and the live
integration test.

## Installation and production boundary

Development install/remove commands remain:

```bash
./apps/macos/scripts/install-virtual-audio-driver.sh \
  apps/macos/Driver/build/Release/CueletVirtualAudio.driver
./apps/macos/scripts/uninstall-virtual-audio-driver.sh
```

Both commands require explicit typed confirmation, administrator approval, and
a manual restart. The bundle is ad-hoc signed, arm64-only, unsandboxed,
non-notarized, and not production-ready. Production requires a supported
Developer ID signing/notarization/update decision and receiving-application
compatibility testing.

## Current limitations and priority

1. Highest priority: install candidate 0.1.3 only after explicit approval,
   restart manually, and repeat the Luna 44.1/48 kHz, two-reader, and five-minute
   live transport suite before accepting the fix.
2. Determine whether the live device-level running property should transition
   for AVFoundation clients or whether the property is being queried at the
   wrong lifecycle/scope.
3. Add physical-microphone mixing only with explicit permission and a visible
   indicator; the driver itself must never open a physical microphone.
4. Add synchronized speaker-plus-virtual routing only after measured drift and
   cleanup work.
5. Complete sleep/wake and broader receiving-application compatibility checks
   only after nonzero transport is restored.

## 0.1.3 post-reboot validation result

The corrected bundle was installed and the Mac was restarted before the Luna
validation session. Identity and Core Audio loading passed: version 0.1.3,
build 4, arm64, executable SHA-256
`44aeb0394f3eab81a6bad3592e0d6c5a9a74b717f959242612ead16521d72b48`, expected
bundle/device/model identifiers, and exactly one enumerated Cuelet device.

The live all-zero defect remains. Deterministic 997 Hz/1499 Hz stereo output
was injected at 44.1 and 48 kHz. The direct HAL receiver, an independent
AudioDeviceIOProc receiver, an independent HAL Output Audio Unit receiver,
producer-first startup, receiver-during-playback startup, and two simultaneous
receivers all returned complete-zero captures. The five-minute 48 kHz capture
returned 14,736,384 zero frames in 307.008 seconds. Timing stayed stable, so
this is not a receiver clock or callback-gap result.

The source-level Luna replay and automated core, contract, sanitizer, and
bundle tests pass, but they do not establish live publication. The remaining
live boundary is the driver's WriteMix-to-ring publication or ReadInput
timeline/generation read path. No source was changed during this validation.
The measured evidence and focused Sol implementation brief are recorded under
`/tmp/cuelet-driver-013-luna-postreboot-20260806-125216/`.

## 0.1.4 diagnostic development build

The passing production-interface replay does not model enough of Core Audio's
live operation to distinguish whether the server calls `WriteMix`, which
buffer and timestamp flags it supplies, whether the ring accepts the data, or
why `ReadInput` later returns zero. The 0.1.4 build changes diagnostics and
version integration only; no additional transport branch was changed because
the SDK review did not independently prove another defect.

The current macOS 26 SDK contract states that `WriteMix` and `ReadInput` are
in-place operations on `ioMainBuffer`. `WillDoIOOperation` advertises both as
supported and in-place. `ioSecondaryBuffer` is not selected. Contract tests
cover nonzero main plus nonzero secondary, main only, missing main, wrong
stream, zero data, stereo interleaving, and variable frame counts. The driver
has one static interface and one shared `gIOState`; every event includes opaque
state and ring tokens so the live inspector can detect an unexpected split.

The preallocated diagnostic ring contains 8,192 fixed-size events. Callback
publication uses only lock-free atomic sequence reservation and fixed-width
atomic stores; the event sequence is release-published after its numeric
payload. External property reads acquire-load and recheck the sequence while
copying a bounded snapshot. Full rings overwrite oldest events and retain
counters. Payload analysis scans no more than 8,192 stereo frames and records
only peak, RMS, zero/nonzero frame counts, first/final sample summaries, and a
deterministic checksum—not the complete application audio.

Private device selectors are:

| FourCC | Meaning |
|---|---|
| `cdsv` | schema/version and fixed capacities |
| `cdct` | cumulative counters since explicit clear |
| `cdev` | bounded event snapshot |
| `cdec` | current event count |
| `cdcl` | explicit clear command (`UInt32` value 1) |
| `cdbv` | diagnostic driver version/build/architecture |
| `cden` | diagnostic-enabled flag |

Write events retain all cycle timestamps and conversion results, selected
buffer, incoming and post-volume/mute payload summaries, resolved absolute
write range, accepted frames, generation, ring slots, and published frame
tags. Read events retain input/output mapping, requested source range, valid
and zero-filled counts, first rejection reason/frame, expected/observed tag and
generation, and returned payload summary. Lifecycle events cover initialization,
StartIO/StopIO reference transitions, ring resets, generation changes,
sample-rate changes, and stream activation.

Outside real-time processing, `cuelet-driver-diagnostics` provides `status`,
`clear`, `snapshot`, `watch`, `watch-events`, and `summarize`. Incremental
500 ms event snapshots preserve startup lifecycle events even if the driver's
bounded ring later overwrites them. The post-reboot workflow is:

```bash
./apps/macos/scripts/run-virtual-audio-live-diagnostics.sh
```

It verifies 0.1.4 build 5 and its exact installed executable hash before
opening clients. It then runs a generated 48 kHz 997/1499 Hz fixture through
direct HAL injector/receiver tools and writes telemetry, capture analysis, and
one concise diagnosis under `/tmp`. It never installs, reboots, restarts
`coreaudiod`, changes defaults, or opens a physical microphone.

Pre-install validation passed 1,222,189 core assertions, 237 driver contract
assertions, 50 recorded Luna replay assertions, 551 telemetry-store assertions,
100,000 stress iterations, ASan/UBSan, TSan, and Clang static analysis with no
remaining diagnostics. Swift Debug and Release each passed 107 tests with two
opt-in skips. The diagnostic bundle smoke test queried build 0.1.4/5 directly
from the loaded candidate interface. Candidate executable SHA-256 is
`cf8896993fcbe6e34d86147a5d2bf6f16c14d33021ec5e17241a1520dcea818e`;
the pre-install evidence root is
`/tmp/cuelet-driver-014-diagnostic-sol-20260806-140202/`.

## 0.1.5 diagnostic property-access correction

The installed 0.1.4 inspector resolved the stable UID to public device object
92 and queried global scope/main element, which matches the driver's intended
address. A Cuelet-only matrix covering the system object, device, input/output
streams, and controls found `AudioObjectHasProperty == false` and
`kAudioHardwareUnknownPropertyError` (`'who?'`) for `cust` and every `cd*`
selector at every tested scope/element. The installed executable and the then
current Release output were byte-identical. This excludes stale inspector
lookup, a wrong stream object, and Release exclusion.

The exact defect was the missing Audio Server custom-property marshalling
contract. Version 0.1.4 implemented raw C-structure branches directly in
`HasProperty`, `GetPropertyDataSize`, `GetPropertyData`, and `SetPropertyData`,
but did not expose `kAudioObjectPropertyCustomPropertyInfoList`. Apple's macOS
26 SDK and current NullAudio sample state that the host only marshals declared
custom properties and only supports `CFString` or `CFPropertyList` payloads.
Core Audio therefore hid the selectors before the inspector request reached
those raw branches.

Candidate 0.1.5 build 6 advertises seven unqualified `CFPropertyList`
selectors from the device object at global scope/main element. `cdsv`, `cdct`,
`cdev`, and `cdbv` return `CFData`; `cdec` returns `CFNumber`; `cdcl` and `cden`
use `CFBoolean`. The event store and transport callbacks are unchanged. Direct
public-interface tests cover the full has/settable/size/get/set/get sequence,
wrong object/scope/element, read-only writes, clear, unknown selectors, and a
diagnostics-disabled build. The Release bundle smoke test loads the actual
candidate executable and verifies the metadata plus build property.

The inspector adds `probe-properties` and selector-specific failures containing
device ID, stable UID, FourCC, scope, element, `HasProperty`, decoded OSStatus,
and required/optional status. The live workflow now retains all partial output,
writes `logs/workflow-error-summary.txt`, and categorizes identity, device or
property access, clear, injector, receiver, snapshot, and analysis failures.

Candidate 0.1.5 remains uninstalled. After explicit installation and a manual
full restart, run:

```bash
./apps/macos/scripts/run-virtual-audio-live-diagnostics.sh
```

Only that post-reboot run can prove that Core Audio now exports the properties
and reveal the unresolved live transport telemetry. No transport fix is claimed.

## 0.1.6 event paging and aggregate rejection diagnosis

Installed 0.1.5 made six of seven private properties usable. Its `cdev`
property alone reported public size 4 and failed fetch with `!siz`, while
`cdsv`, `cdct`, `cdec`, `cdcl`, `cdbv`, and `cden` returned normally. The old
event value was constructed as one monolithic snapshot of the bounded backing
store. Version 0.1.6 instead declares both `cdev` data and qualifier as
`CFPropertyList`, accepts an exact CFData cursor `(schema, maximum count, next
sequence)`, and returns a retained immutable CFData page containing a header
and at most 256 events. The driver callback reports the outer pointer size;
the inspector verifies the returned CF type, exact payload length, schema,
cursor, event size, and sequence progression before decoding.

The public counter path from 0.1.5 measured 672,256 accepted write frames,
including 845 nonzero callbacks, but zero valid frames across 672,256 requested
read frames. Version 0.1.6 diagnostics separate accepted callbacks from stored
payload and release-published tags. Reads now have mutually exclusive
frame-level counters for not-yet-written, overwritten, generation mismatch,
absolute-tag mismatch, unpublished, timeline uninitialized, mapping invalid,
invalid argument, and sample-rate reset. First/last rejection and latest
write/read ranges are fixed-size atomic summaries. The first 32 nonzero writes
and first 32 following reads survive main-ring overwrite.

The existing transport remains unchanged. A follow-up against installed 0.1.5
showed every sampled read failed with `TIMELINE_OUTPUT_INVALID`: the read-side
resolver accepted `mInputTime`, then required an operation-local `mOutputTime`
that live Core Audio did not mark valid, and returned before ring lookup.
Candidate 0.1.6 will preserve the exact input/source flags and classify these
frames as mapping-invalid. This is a precise failure boundary, not yet proof of
the correct replacement timeline origin.

Event polling starts before audio clients and retrieves pages every 100 ms.
Unexpected sequence gaps are reported. If event access still fails, counter
polling and the audio reproduction continue; final counters, injector and
receiver summaries, WAV analysis, `workflow-error-summary.txt`, and
`diagnosis-summary.txt` are always retained before a nonzero exit.

Candidate 0.1.6 remains uninstalled. After explicit installation and a manual
full restart, run:

From the repository root:

```bash
./apps/macos/scripts/run-virtual-audio-live-diagnostics.sh
```

## 0.1.7 focused ReadInput timeline fix

The active SDK defines `mInputTime` as the input data start and `mOutputTime`
as the output data start for the I/O cycle. It also defines
`mIOCycleCounter` as the common cycle ordinal and explicitly permits actual
operation buffer sizes to differ from the nominal size. Nothing in the
ReadInput operation contract makes its unrelated output timestamp
authoritative. The live `input valid / output invalid` operation is therefore
a supported case, not malformed input.

Version 0.1.7 stores one bounded input observation and one bounded output
observation. Matching cycle counter, ring generation, and sample rate establish
the measured `outputStart - inputStart` calibration. A release-published
calibration contains input/output origins, signed frame offset, intentional
one-nominal-buffer delay, generation, and sample-rate bits. ReadInput then
requires only a valid input timestamp and derives:

```text
sourceStart = inputStart + calibratedOffset - loopbackDelay
```

The approximately 184-frame offset seen in earlier runs is not hard-coded.
Before calibration, ReadInput returns deterministic startup silence. A second
reader does not reset calibration, and stopping one client while others remain
active does not invalidate it. Final StopIO, sample-rate changes, and timeline
reset clear the observations and calibration so stale generations cannot be
used.

The production-interface regression that previously returned
`TIMELINE_OUTPUT_INVALID`, skipped `CueletRingReadAt`, and produced zero valid
frames now reaches the ring using stored calibration, returns all 512 expected
frames, and preserves the deterministic stereo checksum. Additional tests
cover all four input/output timestamp-validity combinations, receiver-first
and output-first startup, 44.1/48 kHz, two readers, producer restart, and a
384-frame write consumed as 128- and 256-frame reads. The fixed callback path
uses only bounded atomics and existing fixed storage.

Candidate 0.1.7 remains uninstalled. These results prove the source-level
failure and correction but do not prove live transport or continuity. After an
explicit install and manual full restart, run the same live diagnostic command
above and require nonzero published writes, nonzero valid reads, a nonzero
997/1499 Hz capture, and no unexpected active-transport mapping rejection.
