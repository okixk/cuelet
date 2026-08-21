# Cuelet Cross-Platform Validation

## 1. Validation Scope

This final review covers the Cuelet macOS HAL driver installed at
`/Library/Audio/Plug-Ins/HAL/CueletVirtualAudio.driver`, the current macOS
Release application, the existing Linux implementation and Linux validation
record, the existing Windows implementation and Windows validation records,
and the user-facing documentation. Runtime work was performed only on the
MacBook Air M1 described below. Linux and Windows runtime statements are
transcribed from their platform-specific evidence; they were not run on this
Mac.

Safety boundaries held throughout: no SIP/coreaudiod/default-device changes,
no reboot, no driver reinstall/replacement/removal, no other-vendor driver
changes, no real ambient microphone recording, no real Cuelet library, no
personal-file changes, and no commit.

Environment: macOS 26.6 build 25G72, arm64, MacBook Air M1 (2020), 8 logical
CPUs. Validation roots:

```text
/tmp/cuelet-driver-011-final-20260805-203737
/tmp/cuelet-cross-platform-driver-validation-20260804-192940
/tmp/cuelet-driver-timeline-redesign-20260806-070957
/tmp/cuelet-driver-012-luna-validation-20260806-075517
/tmp/cuelet-driver-013-sol-fix-20260806-090702
/tmp/cuelet-driver-013-luna-postreboot-20260806-125216
```

The installed macOS runtime state is 0.1.5 build 6 and retains the all-zero
live failure after reboot. Its public counters prove nonzero WriteMix calls and
accepted writes, followed by zero valid ReadInput frames. The uninstalled
0.1.7 build 8 diagnostic development build retains the 0.1.6 telemetry and
corrects the demonstrated operation-local output-timestamp dependency in
ReadInput. It is not installed, so this remains pre-install evidence and does
not change Linux or Windows parity conclusions.

## 2. Evidence Levels

- **Runtime**: a real application, Core Audio client, or virtual endpoint was
  exercised on the named platform.
- **Measured**: generated audio was captured and analyzed numerically.
- **Automated**: a test, verifier, sanitizer, or static analyzer passed.
- **Programmatic**: Core Audio/system enumeration or a service result was
  inspected without relying on a screenshot.
- **Source**: current source was inspected; execution was not claimed.
- **Documented**: a platform validation document records the result; it is not
  new runtime evidence from this Mac.
- **Partial/Missing/N/T**: only a subset is proven, no evidence exists, or it
  could not be tested from this host.

The full feature table with per-platform classifications is in
[FEATURE_PARITY_MATRIX.md](FEATURE_PARITY_MATRIX.md).

## 3. Library and Data Parity

The three implementations provide the same core user concept: a sound entry
has a stable identity, metadata, a source mode, and a recoverable missing
state. Managed and linked imports, favorites, categories, notes, aliases,
duplicate handling, safe removal, and atomic persistence are present in the
current platform implementations and covered by their tests or validation
records.

The important semantic differences are now documented: Rename may change a
managed filename but changes only the display name for a linked source; Remove
is metadata-only; Delete Managed File is an explicit destructive action; linked
files remain external; and copied portable metadata does not automatically
authorize access to an arbitrary external path on Linux. These are legitimate
adapter differences, but the labels should remain consistent in future UI
passes.

macOS: runtime plus automated evidence in the Swift tests and isolated app
fixtures. Linux: runtime plus automated evidence in `LINUX_VALIDATION.md`.
Windows: runtime and automated evidence recorded in the Windows UI/runtime
documents, not re-run here.

## 4. Playback Parity

Play, pause, resume, stop, Stop All, simultaneous sounds, progress, duration,
volume, selected output, and device-loss policies exist across the three
implementations. Native audio APIs and cleanup models differ appropriately.

macOS uses `AVAudioPlayer` with explicit stable Core Audio UID routing and
passed the Debug/Release service and live-routing tests. The installed 0.1.2
driver was exercised by two independent low-level receivers after reboot, but
the generated active signal was returned as all zeros. Its final GUI
sound-card play action was not conclusively isolated from an existing Cuelet
process, so this pass makes no Release GUI playback claim.

Linux uses GStreamer and explicit PipeWire/PulseAudio targeting; its validation
record includes real playback, virtual-only, speaker-plus-virtual, and
cleanup. Windows uses MediaPlayer/AudioGraph and its validation records include
real endpoint flow and lifecycle tests.

The main parity gap is routing composition: Linux and Windows support a
speaker-plus-virtual path, while macOS 0.1.1 intentionally supports one
destination at a time.

## 5. Shortcut Parity

Stable sound IDs, local/global shortcut assignment, conflict handling, restore
after restart, rename preservation, and deleted-sound safety are implemented
with native platform mechanisms. macOS Carbon, Linux xdg-desktop-portal
GlobalShortcuts, and Windows global registration are not structurally
identical and should not be made identical. Their user-facing semantics are
reasonably aligned.

macOS and Linux have runtime records for focus changes and restart behavior;
Windows source/tests and its validation documents provide the Windows evidence.

## 6. Virtual Audio Parity

| Platform | Graph | Availability/lifecycle | Capture and monitoring | Evidence |
|---|---|---|---|---|
| macOS | Cuelet output stream -> HAL `WriteMix` absolute sample-time range -> generation-tagged timeline ring -> HAL input range | Installed HAL bundle persists after app exit; app status requires live input/output; manual install/reboot | Installed 0.1.5 receives nonzero WriteMix data but rejects ReadInput when its operation-local output time is invalid; candidate 0.1.7 resolves reads from persistent cycle calibration; physical mic mixing and speaker-plus-virtual are absent | Installed 0.1.5 runtime counters/capture; 0.1.7 automated pre-install suite |
| Linux | GStreamer -> owned PipeWire virtual sink -> PipeWire loopback -> virtual source | User-session nodes/helpers exist only while Cuelet owns them; cleanup is app/process lifecycle | Virtual-only, physical-source mixing, and speaker-plus-virtual were runtime-tested on Ubuntu/GNOME; no default-device change | Documented runtime record |
| Windows | WASAPI/AudioGraph -> SysVAD-derived render endpoint -> kernel bridge/FIFO -> capture endpoint | Installed paired driver persists independently; elevated install/reboot and production signing are required | Physical mic mix and local monitoring are app-side; endpoint flow and lifecycle were runtime-tested in development signing | Documented Windows runtime record; not run here |

Normal-user expectations are equivalent for virtual-only use: select Cuelet's
virtual microphone in the receiving app and play Cuelet audio into it. The
platform-specific lifecycle, permissions, and cleanup behavior must remain
visible in onboarding and diagnostics.

## 7. UI and Accessibility Parity

All three implementations provide a library, search, categories, playback
controls, routing settings, missing-file states, and diagnostics. Native
SwiftUI, GTK/libadwaita, and WinUI controls/layouts are expected to differ.

The current macOS Release application was built, but its fixed bundle
identifier collided with an existing Cuelet process during isolated GUI
validation. Consequently this pass does not claim a conclusive Ready,
version, selected-state, or GUI playback result. Programmatic service and
Core Audio checks confirmed the installed device identity and scopes. Linux
and Windows validation documents contain their own rendered UI evidence.
Important labels/roles exist on all platforms, but a complete
VoiceOver/AT-SPI/Narrator audit is still partial.

## 8. Platform-Specific Differences

- macOS uses a system HAL plug-in, requires an explicit administrator install
  and manual reboot, remains loaded after Cuelet quits, and is currently
  arm64/ad-hoc signed.
- Linux creates transient user-session PipeWire nodes and owned helpers; no
  kernel driver or root installation is needed, and cleanup occurs when Cuelet
  exits. Stock GNOME tray behavior is not promised.
- Windows uses a SysVAD-derived paired kernel endpoint, elevated installation,
  reboot/driver lifecycle handling, and production Microsoft signing for public
  distribution. Development test-signing is not end-user readiness.
- Linux and Windows can mix the physical microphone and/or monitor speakers in
  their documented routes. The current macOS driver deliberately does neither.
- The macOS HAL device and Windows driver can remain available independently of
  the app; Linux virtual nodes are app-owned and transient.

These are legitimate differences when they are explained before selection and
when status/error messages describe the actual state.

## 9. macOS Driver Runtime Results

The installed 0.1.5 build 6 was independently enumerated after reboot under
the expected public name and stable identity. Its executable hash is preserved
in the evidence; current Release output is the uninstalled 0.1.7 corrected diagnostic candidate. The
installed driver exposes two stereo Float32 interleaved
scopes at 44.1/48 kHz, with 128-frame stream-scope latency, 32-frame safety
offset, and a 16,384-frame zero-timestamp period.

The critical live result is a complete transport failure: the deterministic
injector produced continuous 512-frame `WriteMix` output callbacks, while the
direct HAL receiver and an independent `AudioDeviceIOProc` receiver returned
only zero samples at both rates, with one and two clients and during a
306.997-second capture. Receiver timestamps and callback intervals were clean,
so this is not a receiver-duration or callback-scheduling success. Active
waveform, stereo, marker, amplitude, and phase criteria cannot be credited
when no active payload is delivered.

The earlier 0.1.1 active-gap/phase-jump defect is therefore not proven fixed by
the installed timeline redesign. The follow-up source replay resolved one internal
branch: `WriteMix` received a nonzero payload and valid output timestamp but
required an unrelated valid input timestamp, rejected timeline mapping, and
skipped ring publication. Although 0.1.3 fixes that exact dependency and passes
the recorded replay, its installed live path remains zero. Installed 0.1.5
counters now prove accepted nonzero writes and localize every sampled read to
an invalid operation-local output timestamp before ring lookup. Diagnostic
0.1.6 added the missing aggregate and event evidence. Candidate 0.1.7 now
separates cycle calibration from input-only read resolution, but remains
unconfirmed in the loaded driver.

Volume/mute property writes and readback passed at 1.0/0.5/0.25 and mute
on/off, but sample-level scaling cannot be credited because the transport is
already silent. The device-level running property remained 0 while stream
`IsActive` remained 1 and callbacks continued; explicit `StartIO`/`StopIO`
contract tests passed.

## 10. Driver Technical Comparison

| Topic | Windows | Linux | macOS |
|---|---|---|---|
| Audio graph | WASAPI/AudioGraph to paired SysVAD render/capture endpoints | GStreamer to owned PipeWire sink/source graph | Core Audio HAL output/input scopes on one device |
| Injection | render endpoint publishes to kernel bridge | PipeWire virtual sink | HAL `WriteMix` publishes to atomic ring |
| Capture | independent kernel FIFO readers | PipeWire source/client graph | independent HAL input cursors |
| Buffer model | 512 KiB nonpaged circular PCM bridge, bounded reader cursors | PipeWire-managed buffering and helper lifecycle | Installed 0.1.2: 16,384 stereo frames indexed by absolute sample time and reset generation; live reads returned silence despite expected written ranges |
| Formats/rates | 48 kHz, 16-bit, stereo PCM in current driver record | PipeWire negotiates native graph formats | 44.1/48 kHz, Float32 interleaved stereo |
| Latency | Windows runtime record includes bounded drift/lag evidence | PipeWire graph policy; exact value is client/session-dependent | 128-frame stream latency, 32-frame safety offset; formal E2E not accepted |
| Lifecycle | elevated install, repair, endpoint health, reboot/uninstall | transient child processes/nodes | persistent HAL install, manual reboot, app readiness by UID |
| Failure handling | helper rollback/status/pair verification | owned process/node cleanup and reconnect states | bounded ring silence/overrun policy and service status |
| Permissions | UAC/admin; public package must be signed | user-session permissions | admin install; receiving apps need microphone permission |
| Privacy | driver does not open physical mic; app mix is user-mode | optional exact-source mix; no ambient capture in validation | driver never opens physical mic; no mix in 0.1.1 |
| Speaker monitoring | app graph supports it | app graph supports it | not implemented |
| Testing | Windows runtime flow/lifecycle/quality records | live PipeWire + GTK/portal records | installed 0.1.5 identity/counters/direct-receiver stability passed, but active transport returned complete silence; automated model/stress/sanitizer/static tests passed |
| Packaging | production Microsoft signing still required | distribution packaging/runtime integration remains | ad-hoc arm64 local driver; Developer ID/notarization absent |

The internal graphs are not required to match. At the normal virtual-only user
workflow level, the expected behavior is comparable; physical mixing,
speaker-plus-virtual routing, and distribution readiness are not yet equal.

## 11. Confirmed Gaps

- macOS installed 0.1.5 transport still returns complete silence. Public
  counters prove accepted nonzero writes and show every sampled read exits
  timeline mapping with an invalid output timestamp before ring lookup.
  Candidate 0.1.7 corrects that source-level branch and remains uninstalled;
  live transport and continuity are still required.
- The earlier 146.472-second duration mismatch remains an AVFoundation
  delivery/measurement issue in this evidence set, not a confirmed driver
  clock defect.
- macOS live running-property transition was not observed.
- macOS has no physical-microphone mixing and no simultaneous speaker-plus-
  virtual route.
- macOS sleep/wake and several receiving applications were not tested.
- Windows and macOS production signing/notarization are incomplete.
- Accessibility audits and broad receiving-application matrices remain partial.
- Windows/Linux runtime evidence is historical/platform-local evidence, not
  evidence generated on this Mac.

## 12. Recommended Consistency Changes

- Keep Rename, Remove, Delete, linked-file, and missing-file wording aligned;
  explain when a platform-specific action changes a managed filename versus
  display metadata only.
- Use an explicit routing mode label on every platform: speaker-only,
  virtual-only, or speaker-plus-virtual. Do not imply physical microphone mix
  from selecting an output.
- Explain whether virtual audio is transient (Linux) or installed/persistent
  (macOS/Windows), whether admin permission is needed, and whether restart is
  required.
- Preserve stable identity in shortcut and metadata UI, especially after rename
  or relink.
- Make receiver selection guidance explicit: Cuelet Virtual Microphone is the
  input endpoint; the injection/render endpoint is not the microphone selector.

## 13. Release Readiness by Platform

| Platform | Classification | Basis |
|---|---|---|
| macOS | Alpha | Installed 0.1.5 still fails live transport. Candidate 0.1.7 passes its focused source-level regressions but has not passed post-reboot live transport or continuity. Physical mix, speaker-plus-virtual, sleep/wake, and signing limitations remain. |
| Linux | Beta | Linux validation documents real GTK/Wayland/PipeWire runtime flow, mix modes, cleanup, shortcuts, and strict Debug/Release/ASan/UBSan tests; packaging, broader clients, tray, and accessibility remain open. |
| Windows | Beta | Windows validation documents real paired endpoint flow, lifecycle/quality suites, cleanup, and WDK/static checks; distribution still needs production signing and final clean-system release validation. |

## 14. Evidence and Screenshot Manifest

Installed 0.1.2 Luna runtime evidence root:

```text
/tmp/cuelet-driver-012-luna-validation-20260806-075517
```

Candidate 0.1.3 source-fix and pre-install evidence root:

```text
/tmp/cuelet-driver-013-sol-fix-20260806-090702
```

Its manifest is
`/tmp/cuelet-driver-013-sol-fix-20260806-090702/screenshots/manifest.csv`;
the Luna runtime manifest remains in its original root.
The earlier broad UI screenshot set remains in
`/tmp/cuelet-cross-platform-driver-validation-20260804-192940/screenshots/`.
Audio fixtures/captures are under `audio/`; filtered diagnostics, inventories,
verifier output, test output, and resource snapshots are under `logs/`.

Major current evidence includes installed identity/hash, programmatic Core
Audio inventory, generated 44.1/48 kHz captures, independent receiver
captures, two-client captures, the five-minute all-zero result, timeline
correlation, volume/mute property readback, lifecycle stress, automated test
output, and filtered process/resource logs.

Missing or intentionally unclaimed evidence: active nonzero transport,
phase/stereo/marker/amplitude proof for 0.1.2, conclusive isolated Release GUI
Ready/selected/playback interaction, a retained privacy-safe Audio MIDI Setup
screenshot, a QuickTime recording waveform, Safari/Firefox/Teams/Discord/OBS
receiver passes, sleep/wake, physical microphone mixing, speaker-plus-virtual
macOS routing, and a formal end-to-end latency number. The manifest records
excluded or privacy-discarded visual attempts rather than treating them as
driver results.

### Latest macOS post-reboot result

The 0.1.3 build 4 bundle was installed and tested after a full restart. The
expected hash, stable identity, stream layout, rates, and Core Audio enumeration
passed. Live deterministic transport did not: 44.1 and 48 kHz direct captures,
independent receiver captures, two-reader captures, and a 307.008-second
five-minute capture were all zero despite clean callback timing. The current
evidence therefore supersedes the earlier candidate-pending status: 0.1.3 is
installed but the all-zero transport failure remains.

macOS remains Alpha. The failure is localized to the live driver output
publication/input-read boundary; source-level replay and automated tests passing
are not sufficient to claim runtime parity. Evidence root:

```text
/tmp/cuelet-driver-013-luna-postreboot-20260806-125216/
```

### macOS 0.1.4 diagnostic candidate

The next macOS candidate is version 0.1.4 build 5 and is explicitly a
diagnostic development build. It preserves the public device identity,
formats, timeline ring, and 0.1.3 transport decisions. An 8,192-event bounded
numeric ring is exported through private Core Audio device properties and
decoded by a command-line inspector. Live events distinguish incoming versus
post-control WriteMix data, write acceptance/publication tags and generation,
ReadInput source mapping and first rejection, lifecycle resets, and shared
state identity. Callback telemetry has no allocation, blocking, formatting,
filesystem access, IPC, or complete-buffer retention.

The candidate remains pre-install evidence only. A short post-reboot diagnostic
run is needed to identify whether live Core Audio omits WriteMix, supplies a
zero or differently represented buffer, rejects publication, resets generation,
or requests unavailable read ranges. macOS remains Alpha; Linux and Windows
source and their existing evidence levels are unchanged.

Pre-install diagnostics validation passed the complete driver suite,
100,000-iteration stress, ASan/UBSan, TSan, static analysis, diagnostic bundle
smoke, and both Swift configurations. Candidate SHA-256 is
`cf8896993fcbe6e34d86147a5d2bf6f16c14d33021ec5e17241a1520dcea818e`;
evidence root is `/tmp/cuelet-driver-014-diagnostic-sol-20260806-140202/`.

### macOS 0.1.5 live counters and 0.1.6 diagnostic candidate

The installed 0.1.5 public `cdct` counter path completed a real 48 kHz
injector/receiver run. It measured 1,313 WriteMix calls, 845 nonzero callbacks,
672,256 accepted frames, and no rejected frames. It also measured 1,313
ReadInput calls, zero valid frames, and 672,256 zero-filled frames. State and
ring tokens remained stable; two resets, two generation changes, and one
balanced StartIO/StopIO pair were observed. This excludes callback absence,
zero-only injection, and reported write rejection from the remaining boundary.

The 0.1.5 `cdev` event property was present but exposed a four-byte public size
and returned `!siz`; the other diagnostic properties worked. Candidate 0.1.6
uses exact immutable CFData pages with a typed cursor qualifier, adds
publication-stage and mutually exclusive read-rejection counters to `cdct`,
and preserves critical startup events separately. Its workflow treats event
streaming as optional: counter polling, audio clients, WAV analysis, and both
summaries finish before a partial-evidence failure is returned.

A read-only follow-up found 657 successful writes, 657
`READ_TIMELINE_UNINITIALIZED` reads, and 657 read-side
`TIMELINE_OUTPUT_INVALID` results. Source inspection confirms those reads exit
the current resolver after validating input time but before ring lookup because
their operation-local output timestamp is invalid. Candidate 0.1.6 records
that exact boundary as mapping-invalid; it does not change transport mapping.
Its pre-install suite, sanitizers, static analysis, Swift tests, bundle smoke,
and workflow dry runs pass. The candidate SHA-256 is
`9420cd08fb5c38dd30514b80fb14eecabc9a2d61cdd17f6775a6596d8055cb54`;
evidence root is
`/tmp/cuelet-driver-016-sol-diagnostics-20260806-153456/`. macOS remains Alpha,
and Linux/Windows source and evidence levels are unchanged.

### macOS 0.1.7 ReadInput timeline candidate

The active SDK assigns input and output data positions to `mInputTime` and
`mOutputTime` independently and supplies a common I/O-cycle ordinal. The
installed 0.1.5 live shape—valid input time with invalid operation-local
output time—is therefore handled by storing a cycle-paired calibration instead
of requiring both timestamps in every ReadInput operation. The approximately
184-frame relationship observed previously is measured from actual paired
cycle observations, not encoded as a constant.

Candidate 0.1.7 release-publishes a generation- and sample-rate-bound signed
offset and nominal one-buffer loopback delay. Input-only reads use their
authoritative input start plus that stored mapping. Startup before calibration
remains silence; final StopIO, sample-rate changes, and timeline resets
invalidate calibration, while additional readers and intermediate stops do
not. The pre-fix production-interface regression returned
`TIMELINE_OUTPUT_INVALID` and never reached the ring. It now reaches ring
lookup, returns 512 valid nonzero frames, and preserves the 997/1499 Hz payload.

The candidate also passes receiver/output-first ordering, all timestamp-validity
combinations, two-reader and producer-restart behavior, different callback
sizes, 44.1/48 kHz calibration, long timeline simulations, and the retained
diagnostic suite. This is automated/source evidence, not post-reboot runtime
evidence. macOS remains Alpha until installed 0.1.7 produces nonzero continuous
live capture. Linux and Windows source and classifications are unchanged.
