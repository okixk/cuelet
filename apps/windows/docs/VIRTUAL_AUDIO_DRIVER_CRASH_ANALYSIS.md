# Cuelet virtual-audio driver crash and hardening report

Initial audit: 2026-07-19  
Current follow-up: 2026-07-21  
Current candidate: `20.42.0.721`, Debug x64, WDK `10.0.26100.0`  
Status: Stage A attempted; root devnode failed start with `0xC000000D`, then
fully removed. Do not reinstall this build or begin Stage B.

This report is deliberately conservative. The preserved dump does not show a
pool-corruption bugcheck. It shows a deliberate Debug breakpoint in the
crashing Cuelet binary. The code audit found and fixed callback-lifetime,
registry-cleanup, and FIFO-arithmetic defects, but the supplied dump cannot
prove that any one of those defects caused a separate pool corruption.
Consequently, this candidate must not be described as safe or complete.

## 2026-07-21 follow-up hardening

The FIFO, bridge, WaveRT timer, PnP, and adapter paths were reviewed again from
the current dirty repository state. No existing work was reverted. Candidate
`20.42.0.721` adds the following defenses beyond `20.40.0.719`:

- every publish/read copy plan is validated at runtime for status, capacity,
  source span, wrap geometry, integer overflow, and whole-frame alignment;
- the physical ring bounds are checked again immediately before each copy;
  invalid capture plans fail closed to an entire silent quantum;
- elapsed-time-to-byte displacement uses checked 64-bit arithmetic, preserves
  sub-millisecond and fractional-byte carry, and caps catch-up work to one DMA
  buffer after sleep or a long scheduler delay;
- STOP, surprise removal, remove, failed start, and unload all enter the same
  idempotent teardown gate, while a successful restart explicitly resets and
  rearms the static bridge under its spin lock;
- bridge device activity and lifecycle epochs are explicit; rejected lifecycle
  operations and invalid plans are counted, and lifecycle summaries are logged
  outside the per-quantum timing path;
- every lifecycle field used by a teardown summary, including the device epoch,
  is captured under the bridge spin lock;
- every FIFO helper used at `DISPATCH_LEVEL` is forced inline in kernel builds,
  preventing an analyzer-generated pageable out-of-line copy;
- the shared user-mode ring model uses the same runtime plan validation as the
  kernel wrapper; and
- the installer permanently rejects the preserved crash build
  `20.37.42.726`, even when the Debug test-package opt-in is present.

The stream timer retains the previous synchronous destruction rule:
`ExDeleteTimer(Cancel=TRUE, Wait=TRUE)` and queued-DPC flushing happen before
reader release or any callback-visible teardown. The timer owns no independent
bridge allocation; its callback uses the stream only while synchronous timer
destruction keeps that stream alive. The bridge itself remains static,
nonpaged image storage and has no allocation/free or double-free path.

New portable tests cover malformed copy plans, repeated teardown, exact 48 kHz
and fractional 44.1 kHz displacement, and long-delay catch-up clamping. Debug,
Release, and the actual MSVC AddressSanitizer-linked tests pass. ASan required
the matching MSVC runtime directory on `PATH`; the first missing-runtime launch
failed before `main` with `0xC0000135` and was not a sanitizer finding.

The final Debug `/analyze` log contains 2,563 warning lines but only 64 unique
diagnostics: 42 are the inherited unreachable-section INF notices and 22 are
in WDK/WIL headers. There are **zero unique warnings in Cuelet-authored driver
code**. The log is preserved at:

`apps\windows\x64\Debug\DriverAnalysis-20.42.0.721-Final\driver-analysis.log`

Candidate directory:

`apps\windows\x64\Debug\DriverPackage-20.42.0.721-Candidate`

| Candidate artifact | SHA-256 |
|---|---|
| signed SYS | `0597827EDF67A9D26AFA393F39399C77BAA81298A1E6AE1ED82D57A7FEDA2EA3` |
| matching PDB | `E6AEB8E1811F8B24E1838FA1CD514B5DB24F9AD30E964ED78415C564B806B8E8` |
| INF | `B37CF2990B5777852562CE1D3DEC367AA2A893151A27F0598EE6432C679D3CAA` |
| signed CAT | `535B5ECC20DEDD99BE98C4B284C42AF991699D91E82288EEA1F412A801CC2DE1` |
| candidate manifest | `0175838E3F15ABAC0EB3DB990357016E1F6A8F7AF7EEFE7E00F079D1F5E58C76` |

`symchk` reports private symbols and lines, PDB
`{628A9DC0-0160-4EEB-99A1-D046C2532B41}`, age 1, with both PDB and DBG matches.
InfVerif `/w` and `/u` succeed with the 42 documented inherited notices;
Inf2Cat reports zero signability errors and warnings; and SignTool `/pa`
verifies the test-signed SYS and CAT. ApiValidator strict compliance reports
the Release submission and all its binaries as Universal. The separately prepared Release
submission is unsigned and is not an installable production package.
Its matching Release PDB is preserved in the versioned validation archive with
SHA-256 `A65BDAD210C3C27F487980F69EBA31B2A7F6A4ADBD58143C24A700E7D1262687`;
`symchk` matches it as `{0EBD8E2D-558D-466C-8ABB-E00F60F991D0}`, age 1.

An earlier `20.41.0.721` package was preserved without installation and
superseded before runtime testing when the unlocked diagnostic-epoch read was
found. No artifact was silently replaced.

Read-only checks on 2026-07-21 found no Cuelet devnode, service, driver-store
entry, endpoint, or installed package. Driver Verifier has zero flags and no
selected drivers. Volume C: reports NTFS, Healthy, and OK. No driver was loaded,
staged, installed, or audio-tested during this follow-up.

## Stage A runtime result

The later controlled Stage A attempt is recorded separately in
`VIRTUAL_AUDIO_DRIVER_STAGE_A_20.42.0.721.md`. Windows staged the exact package
as `oem5.inf` and attempted to start `ROOT\CUELETVIRTUALAUDIO\0000`, but SetupAPI
reported `CM_PROB_FAILED_START` with `STATUS_INVALID_PARAMETER (0xC000000D)`.
No endpoints were created. Automatic rollback, explicit uninstall, and exact
stale-service cleanup left no Cuelet package, service, devnode, endpoint, or
loaded module. Stage B is blocked.

## Preservation and live state

- Repository HEAD at task start:
  `abf3e888f8e0d60f06dbad91ad33d79f5da48602`, branch `main`.
- Task-start backup:
  `$VALIDATION_ROOT\cuelet-task-backups\20260719-002054`.
- The backup contains all 57 Git-reported files, Git status, unstaged and
  staged diffs, diff statistics, HEAD/branch metadata, crash diagnostics, and
  the driver overlay. Its 115 copied files total 9,160,912 bytes; hash
  verification found zero mismatches.
- Read-only PnP, driver-store, service, `driverquery`, process, and Driver
  Verifier checks found no live Cuelet component. Verifier flags were zero and
  no drivers were selected.
- No driver package was staged, no root devnode was created, and no Cuelet SYS
  was loaded during this investigation.

## Preserved evidence

Evidence root:

`$VALIDATION_ROOT\cuelet-crash-diagnostics\20260718-211513-driver-20.37.42.726`

| Artifact | SHA-256 |
|---|---|
| `dump\071826-8765-01.dmp` | `6B469C9360263545BFEA2CD710ED2B2B02400DBE815392606C499F00A23A81E4` |
| signed `CueletVirtualAudio.sys` | `DA2E8245ECA662B75C2B0809041163D727525E485BECF786AA2E401244E36F16` |
| linker `CueletVirtualAudio.pdb` | `CDD8E3731CFBDFD3378994FF6A5647E8F6952CE658186DA7A6BBFE6193CB9E53` |
| linker unsigned SYS | `EDD797009705B769D0ED902EB0FC44BF859F57FE0CC89B0118C550E60505C6ED` |
| `CueletVirtualAudio.inf` | `796AF3E19BDD845B08354A67C3FD57523BBA18292FD2CDF22712217A606C60D7` |
| `cueletvirtualaudio.cat` | `ADD04528D315ACD905388D97D255B3B9F3952322BF07B770D5535378648C2459` |

The manifest reports `DriverVer=07/18/2026,20.37.42.726`. Every manifest hash
matched. `symchk` matched both the signed and unsigned SYS to the preserved PDB
with private symbols and lines. The crashing PDB identifier is
`{7165B2C0-80D2-40CA-B70E-924E7A7FA44C}`, age 1.

The PDB referenced 98 source files. All 98 embedded source checksums matched
the preserved generated tree. A PDB-verified supplemental snapshot is at:

`$VALIDATION_ROOT\cuelet-driver-analysis\20260719-003000\crashing-source-snapshot`

The source is based on Microsoft SysVAD commit
`2ee527bfeb0aeb6be11f0a8b6dce4011b358ce89`.

## Exact dump result

The required WinDbg commands were run against only the preserved small dump,
Microsoft symbols, and exact local PDB. Logs and command files are under:

`$VALIDATION_ROOT\cuelet-driver-analysis\20260719-003000`

The dump is a mini kernel/triage dump containing registers and a kernel stack.
It reports:

- Bugcheck: `0x1E KMODE_EXCEPTION_NOT_HANDLED`
- Parameter 1: `FFFFFFFF80000003` (`STATUS_BREAKPOINT`)
- Parameter 2: `FFFFF8079404CB19`
- Parameter 3: `0000000000000000`
- Parameter 4: `0000000000000008`
- Instruction: `FFFFF8079404CB19  CC  int 3`
- Function:
  `CueletVirtualAudio!CAdapterCommon::UpdatePowerRelations+0xf9`
- Source: `common.cpp:2387`
- Source statement:
  `DPF(D_ERROR, ("CAdapterCommon::UpdatePowerRelations: No PDOs in power relations"));`
- Saved IRQL: 1 (`APC_LEVEL`)
- Thread: `FFFFD803DBBD8080`
- Process: `WmiPrvSE.exe`
- CPU: 14

The relevant stack is:

```text
nt!KeBugCheckEx
nt!KiDispatchException
nt!KiExceptionDispatch
nt!KiBreakpointTrap
CueletVirtualAudio!CAdapterCommon::UpdatePowerRelations+0xfa
CueletVirtualAudio!PnpHandler+0x175
nt!IopfCallDriver / nt!IofCallDriver
ksthunk
PnP query-device-relations processing
nt!NtPlugPlayControl
WmiPrvSE.exe
```

The adapter candidate was `FFFFD803C6BE2C80`, IRP
`FFFFD803C818DA60`, and device object `FFFFD803D51EFC00`.

`D_ERROR` in the Debug WDK `ksdebug.h` path invokes `DbgBreakPoint()`. The
source comment immediately after the diagnostic says the zero-PDO condition
is not an error and there is simply nothing to do. Therefore the exact cause
of this preserved crash is an unconditional Debug breakpoint on a normal
zero-PDO power-relations query.

This is an execute/breakpoint exception, not an invalid data read or write.
There is no invalid data virtual address to classify. The instruction page is
valid and executable.

### Pool limitations

The pages needed by `!pool`, `!poolval`, `!pte`, and `!address` for candidate
data addresses are not present in this triage dump. Pool ownership, allocation
tags, freed state, corruption state, and an original corrupting write cannot
be recovered. `!locks` and verifier allocation history are also unavailable.

Accordingly, this dump cannot establish:

- a freed or paged allocation;
- an out-of-allocation access;
- the allocation owner;
- a free/destruction path; or
- the reported `DRIVER_CORRUPTED_MMPOOL` bugcheck.

The latter report may refer to a different, unpreserved crash. It must not be
conflated with this `0x1E` dump or with the later filesystem-related incident.

## Version comparison

Working source:

`$VALIDATION_ROOT\cuelet-backups\20260718-201822-before-audio-fidelity-investigation`

Crashing source:

`$VALIDATION_ROOT\cuelet-backups\20260718-211513-before-kernel-crash-analysis`

Only the bridge CPP and prepare script changed. The bridge header, render and
capture integration, adapter, allocation model, and ownership model were
identical.

| Difference in 20.37.42.726 | Kernel effect |
|---|---|
| Added 30 ms startup reserve and per-reader `started` state | Callback timing and cursor policy only |
| Added extensible-format fields and validation | Format matching and integer calculations |
| Aligned usable capacity to frames | Copy geometry; no allocation-size change |
| Reset ring, writer, and readers on format change | Cursor generation/state behavior |
| Dropped only after true capacity overflow | Cursor movement and continuity |
| Returned a whole silent quantum on underflow | Output policy; prevents stale partial data |
| Removed the old “snap to latest request whenever lead exceeds one request” behavior | Eliminates phase discontinuities |
| Prepare-script endpoint topology-name transform | Package/topology metadata only |

Neither version dynamically allocated or freed the shared bridge. Both used a
static 512 KiB ring, static reader slots, and a static spin lock.

## Ownership and lifetime audit

The resulting ownership model is:

1. `DriverEntry` initializes the static bridge and spin lock.
2. The render WaveRT stream publishes rendered bytes through a function call;
   it stores no bridge pointer.
3. The capture WaveRT stream reads through a function call. Its `this` value is
   only an opaque reader key and is never dereferenced by the bridge.
4. Reader cursors live in a static 32-entry table protected by the bridge spin
   lock.
5. Stream pause/reset stops its notification timer.
6. Stream destruction now synchronously deletes the notification timer and
   waits for the timer callback, flushes queued DPCs, then releases its reader
   and callback-visible state.
7. surprise removal, remove-device, and `DriverUnload` begin bridge teardown.
   Later publishes are rejected and reads return silence.
8. The bridge storage is part of the loaded image and is never freed by an
   adapter, miniport, or stream.

There are no independent bridge owners and no bridge double-free path.

### Lifetime defect found by audit

The old stream destructor released the reader and other callback-visible
state before calling `ExDeleteTimer(Cancel=TRUE, Wait=TRUE)`. The timer callback
holds a raw `CMiniportWaveRTStream*` and runs at `DISPATCH_LEVEL`.

A failing interleaving was possible:

1. stream destruction starts at `PASSIVE_LEVEL`;
2. reader/miniport or other stream state is released;
3. the notification timer expires or its DPC enters with the raw stream
   pointer;
4. the callback accesses partially destroyed state;
5. only later does the destructor cancel and wait for the timer.

This is a real use-after-destruction risk and can cause invalid accesses or
secondary corruption. The new rule is that callback quiescence is the first
destructor action. Synchronous timer deletion and `KeFlushQueuedDpcs()` complete
before reader release or any callback-visible free.

The preserved dump does not contain this interleaving, so this finding is a
code-audit-derived candidate cause, not a proven explanation for an
unpreserved pool bugcheck.

## Pool and IRQL audit

| State | Storage/allocation | Tag | Access/free rule |
|---|---|---|---|
| bridge ring (512 KiB) | static driver-image storage | none | nonpaged; never freed |
| bridge readers, FIFO state, diagnostics, spin lock | static driver-image storage | none | nonpaged; never freed |
| WaveRT stream format and callback-visible buffers | `ExAllocatePool2(POOL_FLAG_NON_PAGED, ...)` | `SRWM`/upstream stream tags | freed only after timer/DPC quiescence |
| upstream C++ objects | kernel `new` wrapper using `ExAllocatePool2` | upstream tags | executable pool flags now rejected |
| temporary registry names/information | `ExAllocatePool2(POOL_FLAG_NON_PAGED, ...)` | `MINADAPTER_POOLTAG` | paged routines at `PASSIVE_LEVEL`; every loop cleanup resets pointers |

Bridge publish/read/release functions and their helpers are nonpageable. They
may run at `DISPATCH_LEVEL`, acquire only the initialized `KSPIN_LOCK`, perform
bounded copies, and neither allocate nor free. There is no blocking wait while
holding the bridge lock. Destruction and synchronous timer deletion are
pageable/passive-level operations.

The observed lock order is stream-position lock followed by bridge lock. No
path acquires them in reverse. Beginning teardown acquires only the bridge
lock; acquiring it waits for an in-flight bridge operation and prevents later
operations from entering usable state.

## Additional source defects fixed

Static analysis found two allocation checks in the pinned SysVAD registry-copy
code that tested the already-valid information buffer instead of the newly
allocated name pointer. A failed name allocation could therefore reach
`RtlStringCbCopyNW` with null.

The same loops freed per-iteration pointers and closed per-iteration handles
without clearing them. The destination-key failure path also closed the source
handle before jumping to common cleanup, which could close it a second time.
The candidate checks the correct pointers, has one cleanup owner, and clears
every pointer/handle after cleanup.

The template-interface registration status is now checked before its output is
used. An upstream sample block that registered the current thread as a stream
resource was removed. USB and Bluetooth sideband device implementations are
not compiled into the final Tablet target. This also removes the exact
`UpdatePowerRelations` Debug-breakpoint path shown by the dump.

## FIFO arithmetic hardening

`CueletAudioFifoCore.h` is the one portable implementation used by both the
kernel wrapper and user-mode model. It uses named `Bytes`, `Frames`,
`BytePosition`, and `Generation` values.

It now enforces:

- capacity, reads, writes, reserve, and copy plans aligned to whole frames;
- 64-bit checked frame-to-byte and sample-rate multiplication;
- PCM/extensible-PCM and valid-bits validation;
- frame-aligned usable capacity instead of modulo by physical 524,288 bytes;
- bounded first/second wrap-copy segments;
- a new generation and cursor rebase before 64-bit wrap;
- reader generation synchronization without unsigned subtraction underflow;
- dropping only when a reader is beyond actual capacity;
- whole-quantum silence without cursor consumption on underflow;
- no stale sample exposure after reset;
- rejection of null, unaligned, inconsistent, overflowed, or teardown-time
  operations.

Debug assertions mirror the copy bounds, but release safety uses runtime
checks and does not depend on assertions.

## Tests

The normal Debug and actual MSVC AddressSanitizer-linked test executables both
passed. The ASan binary import of
`clang_rt.asan_dynamic-x86_64.dll` was explicitly verified.

The portable tests cover:

- 40, 80, 100, 440, and 997 Hz over many wraps;
- multitone, impulse, silence, and alternating stereo min/max;
- exact capacity, capacity minus one frame, and capacity plus one frame;
- non-divisor frame sizes and frame-aligned usable capacity;
- tiny writes/large reads, large writes/tiny reads, and every frame boundary;
- startup reserve near wrap, true overflow, whole-quantum underflow, and no
  stale-data consumption;
- multiple readers, reader recreation, reset during simulated read/write,
  unaligned rejection, checked multiplication, generation change, and cursor
  rebase near `UINT64_MAX`;
- 3,000 start/stop cycles;
- one writer/one reader transferring and validating 40,000 frames;
- one writer with 1,000 reader recreations;
- 1,000 resets while writer and reader threads are active;
- teardown requested during active operations, later-write rejection, and
  capture silence.

The complete Windows Debug solution and its core/application test target also
built and passed without loading the driver.

## Static analysis and packaging

- Production driver compile: Debug x64, WDK `10.0.26100.0`, `/WX`, success.
- Code Analysis for Drivers:
  - endpoint library: 0 unique non-Windows-Kit warnings;
  - final driver: 0 unique non-Windows-Kit warnings.
- The VS 18 compiler with WDK 26100's VS 17 driver tasks emits analyzer
  diagnostics inside WDK/WDF headers (1,350 endpoint-library and 1,171 driver
  warning lines). They were retained in the external logs, not suppressed.
- InfVerif `/w`: success.
- InfVerif `/u`: success.
- InfVerif reports 42 warning 2083 entries for inherited but unreachable
  SysVAD sample endpoint/APO sections. They do not involve memory, IRQL,
  synchronization, or the active install sections.
- Inf2Cat: signability test completed with zero errors and zero warnings.
- SYS and CAT Authenticode `/pa` verification: success with the existing
  `CN=Cuelet Virtual Audio Development` certificate.
- Candidate SYS/PDB private symbols and lines: exact `symchk` match,
  PDB `{5A31CDA9-1999-4F19-AC4A-D1FAFD4602D8}`, age 1.

Candidate directory:

`$REPO\apps\windows\x64\Debug\DriverPackage-20.40.0.719-Candidate`

| Candidate artifact | SHA-256 |
|---|---|
| signed SYS | `4C2F6F5A06F8BF492CF71DA97A5630EC751FB2B23D035B49B32AC4CD96E3C818` |
| PDB | `F3E26F3B04BA3F195D4EF24038364C9A83934418777D3D49FA7F99CBAD7BEA4B` |
| INF | `D1112FA0A56E184D21E9DEC32C66E8D5371DE018C77D0C3D5B14859E9F2B57A6` |
| signed CAT | `972270F342465548018273352AE62D4866438F5134F11DA5E9EE3D4723BE87F6` |

The conventional `DriverPackage` directory still contains the preserved
20.37.42.726 crashing package and was not overwritten.

## Conclusion and remaining uncertainty

The exact preserved crash was caused by a Debug diagnostic breakpoint, not by
the bridge ring buffer and not by an observable pool access. The candidate
removes that compiled sideband path and fixes real lifetime, cleanup, and
arithmetic defects found in the audit.

What remains unknown is the exact origin of the separately reported
`DRIVER_CORRUPTED_MMPOOL`. Proving or disproving the timer use-after-destruction
as its cause requires a full kernel dump or a reproducible isolated-machine
failure with targeted verifier. The candidate has not received kernel-mode
runtime testing and must remain an untrusted test candidate.
