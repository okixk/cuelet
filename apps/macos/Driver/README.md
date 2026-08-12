# Cuelet Virtual Audio driver source

This directory builds the native `Cuelet Virtual Microphone` as a Core Audio Audio Server Driver Plug-in. It is independent of the SwiftPM application and uses the active Xcode command-line tools and macOS SDK directly.

The property and CFPlugIn architecture follows Apple's current [Creating an Audio Server Driver Plug-in](https://developer.apple.com/documentation/coreaudio/creating-an-audio-server-driver-plug-in) sample. `APPLE_SAMPLE_LICENSE.txt` preserves the sample's license and provenance. Cuelet's implementation is a fixed four-object audio graph plus four functional controls, with its own bounded multi-reader loopback core and tests.

Build either local configuration from `apps/macos`:

```bash
./scripts/build-virtual-audio-driver.sh Debug
./scripts/build-virtual-audio-driver.sh Release
```

Run ordinary and sanitizer tests without installing anything:

```bash
make -C Driver test
make -C Driver bundle-smoke CONFIGURATION=Release
make -C Driver stress-test
make -C Driver asan-test
make -C Driver ubsan-test
make -C Driver tsan-test
make -C Driver analyze
```

The deterministic bundle paths are:

```text
Driver/build/Debug/CueletVirtualAudio.driver
Driver/build/Release/CueletVirtualAudio.driver
```

Local bundles are ad-hoc signed and arm64-only. Public distribution still
requires the appropriate Apple signing, hardened-runtime review, notarization,
and packaging.

## Production and diagnostic builds

`Debug` includes bounded event telemetry and builds the inspector/fixture
tools. `Release` disables event telemetry and builds only the HAL bundle. The
same validated transport sources are used in both configurations. An explicit
`CUELET_DRIVER_DIAGNOSTICS=1` override is available solely for developer
validation.

The current driver identity is 0.1.11 build 12. Its transport compatibility
baseline is 0.1.8 build 9; intervening identities changed diagnostics only.
The event snapshot uses wire selector `cqev` and remains read-only,
unqualified, stateless, bounded to 256 records, and available only in a
diagnostic build.

## Transport development history

Version 0.1.2 introduced absolute sample-time indexing, but its live
`WriteMix` path required both `mOutputTime` and `mInputTime` to be valid before
publishing output. Core Audio supplied a valid output timestamp without an
input timestamp for the observed output operation. The mapping resolver
therefore rejected a nonzero 997/1499 Hz payload before
`CueletRingWriteAt`, leaving every later `ReadInput` range unpublished and
zero-filled. The ring itself returned the exact payload and checksum when the
same Luna range was written directly.

Version 0.1.3 changed only that demonstrated publication dependency:
`WriteMix` validates and indexes its own `mOutputTime` independently;
`ReadInput` continues to establish the input/output origin mapping from a
cycle that actually supplies both timestamps. Bounded result codes distinguish
invalid timestamps, generation mismatches, unpublished/not-yet-written,
overwritten, absolute-frame mismatch, and partial reads. The Luna event replay
writes and reads `[122552,123064)` with the original nonzero checksum, but the
equivalent installed Core Audio path remains silent. The replay therefore
omits at least one live condition and does not prove another transport fix.

Each frame still carries an absolute frame, timeline generation, and atomically
published stereo payload. Publication stores payload and generation before a
release store of the absolute-frame tag; readers acquire and recheck that tag
around payload access. Readers never advance to a newest window or accept a
slot merely because it contains some data. Valid portions of partial ranges
are preserved while unavailable frames become deterministic silence. All
real-time callbacks remain allocation-free, nonblocking, bounded, and free of
file I/O or formatted logging.

The fixed loopback delay is one nominal Core Audio IO buffer. Calibration
matches authoritative input and output observations from the same Core Audio
cycle ordinal and stores `outputStart - inputStart`; the timestamps do not
have to be valid in the same operation callback. Subsequent input-only reads
use that fixed mapping until the final client stops, the timeline resets, or
the sample rate changes. A diagnostic-only bundle additionally records
cycle ranges, payload checksum/peak/RMS, validity counts, and reset-generation
decisions in a bounded event buffer.

## 0.1.4 live diagnostic interface

Version 0.1.4 keeps the 0.1.3 transport path unchanged and adds an 8,192-event
preallocated telemetry ring. Real-time producers reserve a sequence with a
lock-free atomic increment, write fixed-width numeric words, and publish the
slot sequence with release ordering. Readers use acquire/recheck semantics
outside the callback. Oldest events are overwritten when full and an overwrite
counter is retained. No callback allocates, blocks, formats text, writes a
file, opens IPC, or emits `os_log`; payload work is capped at 8,192 stereo
frames per callback.

Version 0.1.4 intended private device property selectors for schema (`cdsv`),
counters (`cdct`), event snapshot (historically `cdev`/`cdes`, now `cqev`), event count (`cdec`), explicit
clear (`cdcl`), build identity (`cdbv`), and enabled state (`cden`). Its direct
driver-interface tests passed raw C structures, but the installed host returned
`kAudioHardwareUnknownPropertyError` for every selector because those raw values
cannot cross the Audio Server plug-in process boundary without metadata.
Events carry timestamp flags and original Float64 bits, validated integer
frames, state/ring tokens, lifecycle and generation changes, input/output
ranges, tags, ring rejection reasons, and separate summaries for incoming
WriteMix data and post-volume/mute data offered to ring publication. ReadInput
events distinguish valid frames from deterministic zero-fill and retain the
first rejected frame, tag, and generation.

Build and query the diagnostic tools with:

```bash
make -C Driver tools
Driver/build/Tools/cuelet-driver-diagnostics status
Driver/build/Tools/cuelet-driver-diagnostics clear
Driver/build/Tools/cuelet-driver-diagnostics snapshot /tmp/cuelet-events.jsonl
Driver/build/Tools/cuelet-driver-diagnostics summarize
Driver/build/Tools/cuelet-driver-diagnostics watch-events 15 50 /tmp/cuelet-event-stream.jsonl
```

## 0.1.5 custom-property access correction

Apple's current SDK and NullAudio sample require an Audio Server plug-in object
to advertise each custom selector through
`kAudioObjectPropertyCustomPropertyInfoList` (`cust`). The only supported
cross-process custom value types are `CFString` and `CFPropertyList`. Candidate
0.1.5 adds the seven descriptors to the Cuelet device at global scope/main
element and declares unqualified `CFPropertyList` data. Schema, counters,
events, and build identity use `CFData`; event count uses `CFNumber`; clear and
enabled use `CFBoolean`. The diagnostic payload and audio transport are
unchanged.

## 0.1.6 paged events and read-rejection diagnostics

The installed 0.1.5 live run proved that `WriteMix` was called 1,313 times,
845 incoming callbacks were nonzero, and all 672,256 write frames were
accepted. The same run called `ReadInput` 1,313 times, returned zero valid
frames, and zero-filled all 672,256 requested frames. It had two resets, two
generation changes, and one balanced StartIO/StopIO pair. The writer and reader
reported the same state and ring tokens. This confines the defect to the
publication/read-mapping boundary; it does not prove a transport correction.

The `cdev` failure was initially attributed to its monolithic event export. Although the
driver callback returned a CF property-list reference, the public 0.1.5 host
boundary advertised four bytes and rejected the fetch with
`kAudioHardwareBadPropertySizeError` (`!siz`). Version 0.1.6 declares `cdev`
as `CFPropertyList` data qualified by a `CFPropertyList` cursor and returns a
retained, immutable, exact-length `CFData` page. The later 0.1.10 audit proved
that the reserved-selector collision, not payload size alone, controlled the
public four-byte representation. The callback's outer value is
still one `CFPropertyListRef`; the CFData contains a validated header and at
most 256 fixed-size events. Empty pages, wrapped storage, serialization,
ownership, invalid qualifiers, and insufficient output buffers are covered by
contract tests.

`cdct` remains the independent fallback and now separates input, validated,
stored, and release-published write frames. It classifies every unavailable
read frame as not-yet-written, overwritten, generation-mismatched,
absolute-tag-mismatched, unpublished, timeline-uninitialized,
mapping-invalid, invalid-argument, or sample-rate-reset. First/last rejection,
last accepted/published write, last resolved read, and the first 32 nonzero
writes plus the first 32 following reads are retained in fixed-size atomic
summaries even after the main event ring wraps.

A follow-up read-only query against installed 0.1.5 exposed a more precise
live boundary: all 657 sampled write calls were `WRITE_OK`, while all 657 read
calls were `READ_TIMELINE_UNINITIALIZED`; timeline counters were 657
`TIMELINE_OK` writes and 657 `TIMELINE_OUTPUT_INVALID` reads. In source,
`CueletResolveTimelineMapping` accepts the live input timestamp, then rejects
the operation because `mOutputTime` is not valid, before `CueletRingReadAt` is
called. Version 0.1.6 records that as `READ_MAPPING_INVALID` together with the
input flags/range. Transport behavior is intentionally unchanged because the
correct cross-operation source mapping has not yet been proven.

`cuelet-driver-diagnostics probe-properties` prints the Cuelet-only object
hierarchy and selector/scope/element matrix. Other commands now report the
located device, UID, FourCC, numeric address, `HasProperty`, decoded OSStatus,
and whether a missing selector is required or optional. The workflow preserves
partial output and writes `logs/workflow-error-summary.txt` before returning a
nonzero status for identity, property, clear, injector, receiver, snapshot, or
analysis failures.

After explicit installation and a manual full restart, run from the repository
root:

```bash
./apps/macos/scripts/run-virtual-audio-live-diagnostics.sh
```

The workflow verifies the installed version and hash, clears telemetry, runs
only generated 997/1499 Hz output through the named virtual input, and saves
counter polling, incrementally preserved and final decoded events,
receiver/injector telemetry, a WAV
capture, numeric capture analysis, and one diagnosis summary under `/tmp`.
It does not install, restart Core Audio, reboot, select a system default, or
open a physical microphone.

## 0.1.7 ReadInput timeline calibration

The installed 0.1.5 read-only diagnostics provided the missing source-level
condition: 657 writes completed as `WRITE_OK`, while 657 reads returned
`READ_TIMELINE_UNINITIALIZED` with `TIMELINE_OUTPUT_INVALID`. Live ReadInput
operations had a valid `mInputTime` and no valid operation-local
`mOutputTime`. The old resolver rejected that one-sided timing shape before
calling the timeline ring.

Version 0.1.7 separates calibration from per-operation read resolution. An
input or output operation publishes a bounded observation keyed by the Core
Audio I/O cycle counter, ring generation, and sample rate. Matching
authoritative observations establish the measured input/output frame offset;
the offset is never hard-coded. Once release-published, ReadInput validates
only its authoritative `mInputTime`, checks that calibration still matches the
current ring generation and sample rate, and derives the delayed source range
from the stored offset. Reads before calibration remain deterministic silence.

Calibration is shared by all readers, survives intermediate client starts and
stops, and is invalidated on final StopIO, explicit timeline reset, and sample
rate change. Publication uses fixed-size atomics and bounded retries; the
real-time path performs no allocation, blocking, sleep, logging, file I/O, or
IPC. Contract tests cover receiver-first and output-first startup, valid-input/
invalid-output reads at 44.1 and 48 kHz, the full timestamp-validity matrix,
additional readers, producer restart, and unequal callback sizes. The exact
live-style regression now reaches `CueletRingReadAt`, returns 512 valid frames,
and preserves the deterministic 997/1499 Hz payload checksum.

That pre-install evidence preceded the explicit 0.1.7 installation and manual
restart. The subsequent live run restored nonzero transport but exposed the
separate per-client reader failure documented below. The 0.1.7 pre-install run
passed 1,222,189 core assertions, 23,431 diagnostic
driver-interface assertions, 4,292 diagnostics-disabled assertions, 50 Luna
replay assertions, and 1,123 telemetry-store assertions, plus 100,000 stress
iterations, ASan/UBSan, standalone UBSan, TSan, static analysis, bundle smoke,
and 107 Swift tests with two opt-in skips in both Debug and Release.

## 0.1.8 intermittent per-client reader fix

The 0.1.7 live result proved calibration and absolute mapping were working:
`TIMELINE_OK` accompanied the intermittent failed reads, and successful reads
preserved their corresponding write checksums. The failure diagnostics kept
all three slot-sampling fields at `UINT64_MAX`, proving the failure was before
the absolute ring lookup.

The exact branch was the `reader->initialized == false` early return in
`CueletRingReadAt`. Core Audio can issue a mapped ReadInput operation with a
client ID whose fixed reader slot has been added but has not received its own
StartIO transition while another client keeps the device globally active.
That per-client lifecycle latch therefore cannot decide whether an absolute
ring range exists. Version 0.1.8 atomically adopts the current coherent ring
generation for that selected reader, then continues through the existing
authoritative generation and absolute-frame tag checks. It does not add a
retry loop or change timeline calibration, mapping, publication, reset, or
sample-rate behavior.

The deterministic regression alternates started and not-yet-started input
client slots over the recorded 512-frame ranges beginning at frame 72,528.
Before the correction, all callbacks assigned to the latter slot returned
before metadata sampling. After the correction, all 14 calls report mapping,
generation, pre-ring, and lookup progress, return 7,168 valid frames, and
preserve the corresponding write payloads. Dedicated concurrency tests cover
the shared-reader adoption, stable calibration under observation churn, and
irrelevant global publication-window churn.

Diagnostic schema 3 separated mapping-uninitialized, mapping-invalid,
stream-inactive, and missing-client-reader results and reports mapped,
generation-resolved, pre-ring, ring-lookup, and lookup-unavailable counters.
That generation's event inspector accepted Core Audio's four-byte
qualified-property size proxy, but the later reserved-selector audit showed
that no CFPropertyList value could be fetched through that proxy. The 0.1.10
inspector requires the same pointer-sized outer value as every working
CFPropertyList selector. The capture analyzer reports 997/1499 Hz frequency,
active zero-run locations, phase discontinuities, and marker ordering.

At that stage this was pre-install evidence; the later live validation record
supersedes that gate.

## 0.1.9 through 0.1.11 diagnostic event-property correction

Version 0.1.9 removed the event qualifier and stored a per-process cursor when
the client set `cdev`, but the public Set and Get operations still failed with
`!siz`. A byte-for-byte audit showed that `cdct`, `cdev`, and `cdbv` all had
valid 12-byte `AudioServerPlugInCustomPropertyInfo` entries with `plst` data
and no qualifier. The differing public size did not originate in Cuelet's
metadata or size dispatch: Core Audio itself defines
`kAudioTransportManagerCreateEndPointDevice = 'cdev'` and applies that
property's four-byte `AudioObjectID` representation before Cuelet's custom
property value crosses the public boundary.

Version 0.1.10 moved the logical diagnostic event snapshot to the SDK-unlisted
wire selector `cdes`. Its declaration matched `cdct` exactly: unqualified
`CFPropertyList`, read-only, and pointer-sized at the driver interface. There
is no event-property Set implementation, PID table, cursor slot, cursor mutex,
or cursor lifetime. A non-real-time Get builds one immutable `CFData` snapshot
with schema 4, fixed record size, full-store oldest/newest sequence, returned
first/last sequence, returned and available counts, dropped count, and the
newest 256 records. The inspector retains progression locally, suppresses
overlap, and emits explicit gaps when a poll misses more than one bounded
window. Separate critical write/read evidence remains unchanged.

The installed public 0.1.10 path nevertheless reports `cdes` as settable and
returns `CFDictionary { kind = 0; }`. A cross-device probe reproduced that
same value on all six installed devices, including built-in Apple devices and
other drivers whose `cust` lists do not contain `cdes`. Directly loading the
same Cuelet executable and invoking its driver callbacks returns read-only
schema-4 `CFData`, proving that the host shadows the custom property before the
Cuelet callback. Version 0.1.11 changes only the authoritative wire selector
to `cqev`, which has no macOS 26.5 SDK match and no public property on any
installed device. The stateless format, capacity, local cursor, and audio
transport are unchanged.

The direct diagnostic receiver is built without AVFoundation:

```bash
clang -O2 -Wall -Wextra -Werror \
  -framework AudioToolbox -framework CoreAudio -framework CoreFoundation \
  tools/cuelet-auhal-receiver.c -o /tmp/cuelet-auhal-receiver
```

It selects the device by stable UID, enables only the HAL Output Audio Unit's
input bus, renders through a real input callback, and writes WAV plus
machine-readable JSONL outside the callback. The callback records frame count,
sample/host timestamps, flags, size changes, jumps, gaps, render errors, and
bounded-queue drops. The timing baseline and the post-reboot 0.1.1 discontinuity
reproduction are documented in
`docs/cross-platform-catch-up/MACOS_VALIDATION.md`.

The Audio Server Plug-in's zero timestamp is a sample/host timeline contract;
the transport uses cycle timestamps as its data coordinate rather than
callback arrival order. Input and output operations are not assumed to carry
both timestamps, arrive in the same order, or use the same frame count.
The 0.1.6 diagnostic candidate passed 1,222,189 core assertions, 9,937
diagnostic driver-interface assertions, 136 diagnostics-disabled interface
assertions, 50 exact Luna replay assertions, and 1,123 telemetry-store
assertions including concurrent overwrite/snapshot publication. It also passed
305-second 44.1/48 kHz simulations, two-reader and seeded randomized ordering,
100,000-iteration stress, ASan/UBSan, standalone UBSan, TSan, static analysis, diagnostic bundle
smoke, and Swift Debug/Release tests (107 tests, 2 opt-in skips, no failures per
configuration). Its telemetry still requires explicit installation, a manual
restart, and a short live diagnostic run. No transport repair is claimed.

The installed 0.1.5 executable is preserved at the system destination until
explicit approval is given.
A device-level running-property transition is not assumed merely because a
client opened the input scope; explicit StartIO/StopIO contract behavior is
tested separately, and the live device property remained 0 during I/O on this
host.

To install a future corrected bundle, obtain explicit approval first, then run
from the repository root:

```bash
./apps/macos/scripts/install-virtual-audio-driver.sh \
  apps/macos/Driver/build/Release/CueletVirtualAudio.driver
```

The script verifies the source, requires typing `INSTALL`, backs up an
existing Cuelet bundle as
`/Library/Audio/Plug-Ins/HAL/CueletVirtualAudio.driver.backup-<timestamp>`,
and requires a manual full restart. It does not kill `coreaudiod` or change
system defaults. Do not run the command as part of pre-install validation.
