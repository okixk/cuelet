# Cuelet Virtual Microphone for macOS

Current release-candidate status: Cuelet Virtual Microphone 0.1.11 build 12
passed exact-HEAD post-restart transport and continuity validation on
2026-08-21. The authoritative measurements and candidate hashes are in
[MACOS_VALIDATION.md](MACOS_VALIDATION.md). Earlier 0.1.1–0.1.7 investigation
details are historical and remain available in Git history.

## Identity and artifact boundary

| Item | Value |
|---|---|
| Bundle | `CueletVirtualAudio.driver` |
| Bundle identifier | `ch.oki.cuelet.virtual-microphone.driver` |
| Device name | `Cuelet Virtual Microphone` |
| Device UID | `ch.oki.cuelet.virtual-microphone` |
| Model UID | `ch.oki.cuelet.virtual-microphone.model` |
| Version/build | 0.1.11 / 12 |
| Architecture | arm64 |
| Minimum macOS | 14.0 |
| Production executable SHA-256 | `f269a9c9b75327431925cfde9b1b0f403a5493f04288d29e91edf8af42d80549` |
| Diagnostic executable SHA-256 | `9cad2be160bc79de737b71aa4cd8a0e94b8ac241193322a3d7c4a5dcd2a839c8` |

Release builds disable diagnostic properties and are the only driver variant
embedded in `Cuelet.app` and the local release package. The diagnostic variant
uses the same transport source and public device identity, but exposes a
bounded read-only telemetry surface used by the committed live workflow. It is
a development-validation artifact, not a public package payload.

Local builds are arm64 and ad-hoc signed. Public distribution still requires
Developer ID signing, notarization, and stapling.

## Audio Server Plug-in graph

The plug-in presents one Core Audio device with:

- one stereo Float32 interleaved injection/output stream;
- one stereo Float32 interleaved loopback/input stream;
- volume and mute controls on both scopes;
- 44.1 and 48 kHz support; and
- stable plug-in, device, model, and stream identities.

Cuelet selects the device's output UID for playback. Receiving applications
select the same named device as an input. The driver does not open a physical
microphone, change audio defaults, or send audio to speakers.

## Transport model

`WriteMix` publishes output frames into a fixed 16,384-frame stereo timeline
ring. Every slot carries an absolute frame tag and timeline generation.
Payload and generation are written before release publication of the absolute
tag; readers acquire and recheck the tag around payload access.

`ReadInput` maps its authoritative input sample time to an output source range
using a measured input/output cycle calibration and one nominal I/O-buffer
delay. Calibration is bound to sample rate and ring generation. It is cleared
on final client stop, explicit timeline reset, or sample-rate change.

Readers never jump to the newest data. An exact requested range is returned
when its generation and absolute tags match; unavailable portions are filled
with deterministic silence. Multiple clients use independent fixed reader
slots. A mapped reader that has not received its own StartIO transition adopts
the current coherent generation once and then follows the same exact-range
rules.

The real-time path is allocation-free, bounded, nonblocking, and performs no
formatted logging, file I/O, IPC, or Objective-C/Swift calls.

## Diagnostic surface

Diagnostic builds advertise seven unqualified `CFPropertyList` custom
properties at device/global/main:

- schema (`cdsv`);
- aggregate counters (`cdct`);
- stateless event snapshot (`cqev`);
- event count (`cdec`);
- explicit clear (`cdcl`);
- build identity (`cdbv`); and
- enabled state (`cden`).

The event store is preallocated and bounded to 8,192 records. A snapshot
returns at most the newest 256 records, and client tools retain their own
cursor. Event-store overwrite counts mean older diagnostic records were
replaced; they do not indicate dropped audio frames. Aggregate counters and
critical write/read summaries remain available independently.

## Installation and activation

The public installation path is the two-component Cuelet Installer package.
It installs the app and production HAL system-wide, verifies identity and
version, and touches the installed driver plist so Cuelet can distinguish a
new on-disk bundle from an endpoint loaded before installation.

Installation does not kill or restart `coreaudiod`, change a default device,
or force a system restart. Cuelet reports `Restart required` until the next
normal macOS restart. After restart it requires the stable UID plus both live
stream directions before reporting `Driver ready`; selecting the virtual
output changes that state to `Driver ready — selected`.

Driver developers may use the committed manual installer for a separately
verified diagnostic bundle. That path requires typed confirmation,
administrator authorization, backs up the existing Cuelet bundle, and still
requires a normal restart. It is not an end-user distribution path.

## Exact 0.1.11 live result

The diagnostics-enabled candidate at SHA-256
`9cad2be160bc79de737b71aa4cd8a0e94b8ac241193322a3d7c4a5dcd2a839c8` was
installed and loaded by a normal restart. The committed workflow verified
installed/candidate hash equality before I/O and completed with no workflow
failures.

At 48 kHz stereo, 479,744 injector frames produced 672,256 captured receiver
frames over 14.005333 seconds. The driver published 672,256 frames with zero
publication failures and returned 671,744 valid frames after one expected
512-frame pre-calibration startup block. The capture contained 432,000 active
nonzero frames at the expected 997/1499 Hz, with zero active holes and zero
phase discontinuities. Receiver block and event drops were zero.

A separate exact-candidate run routed real Cuelet playback through the virtual
input. It captured 480,256 stereo frames over 10.005333 seconds, including
350,047 nonzero frames, with zero receiver drops, zero active holes, and zero
publication failures. Cuelet simultaneously reported `Driver ready — selected`.

## Current validation commands

From the repository root or `apps/macos` as shown by each script:

```bash
cd apps/macos
./scripts/build-virtual-audio-driver.sh Debug
./scripts/build-virtual-audio-driver.sh Release
./scripts/test-virtual-audio-driver.sh
./scripts/package-virtual-audio-driver.sh Release
./scripts/verify-virtual-audio-driver.sh \
  Driver/build/Release/CueletVirtualAudio.driver
```

Additional Make targets cover the exact replay, production and diagnostic
contract surfaces, telemetry store, 100,000-iteration stress, combined
ASan/UBSan, standalone UBSan, TSan, and Clang static analysis.

After an explicitly approved diagnostic installation and normal restart:

```bash
./apps/macos/scripts/run-virtual-audio-live-diagnostics.sh
```

The workflow verifies 0.1.11/12 and the pinned diagnostic hash before opening
the device. It records only a generated signal from the named virtual endpoint.

## Supported scope and limitations

Supported in macOS 0.1.0:

- Cuelet-only injection into the virtual microphone;
- receiving applications selecting the virtual device as input;
- stable explicit output routing without changing the system default; and
- normal System Output playback when the virtual route is not selected.

Not implemented or claimed:

- physical-microphone mixing;
- simultaneous speaker monitoring and virtual output;
- changing macOS input/output defaults;
- a privileged helper inside Cuelet;
- production signing/notarization; or
- a broad sleep/wake and third-party receiver certification matrix.
