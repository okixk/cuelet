# Cuelet 0.1.0 cross-platform validation

This record summarizes the platform-specific release evidence current on
2026-08-21. Runtime claims come only from the named platform record; no result
is promoted from source inspection alone. Generated packages, applications,
drivers, recordings, logs, and machine-local validation paths are not tracked.

## Evidence sources

- [macOS validation](MACOS_VALIDATION.md): exact-current-HEAD build, normal
  Installer flow, two normal restarts, exact installed/loaded driver identity,
  measured virtual transport, real Cuelet routing, tests, sanitizers, and local
  package checks on macOS.
- [Linux validation](LINUX_VALIDATION.md): GTK/Wayland/PipeWire runtime evidence
  plus the latest clean build, test, sanitizer, and release-archive audit.
- [Windows validation](WINDOWS_VALIDATION.md): final current-cycle Windows 11
  Release rebuild, installed VB-CABLE runtime evidence, unsigned MSIX audit,
  and publication boundary.

The three platforms use native implementations and do not need identical
internal audio graphs. User-facing capability and the limits of each supported
0.1.0 path are the parity criteria.

## Current release evidence

| Platform | Candidate evidence | Functional result | Distribution boundary |
|---|---|---|---|
| macOS | Cuelet 0.1.0 build 1 and driver 0.1.11 build 12 at Git `3084912f994a1d457c67d38d6184933f72ea8160` | Exact diagnostic/live candidate loaded after normal restart; 48 kHz stereo Float32 transport, 432,000/432,000 active frames, zero drops/holes/phase discontinuities, real Cuelet playback, and normal System Output playback passed | Developer ID signing, notarization, stapling, and public Gatekeeper validation remain; physical-microphone mixing and simultaneous local monitoring are not claimed |
| Linux | Native Ubuntu 26.04 GNOME/Wayland x86_64 Release build at Git `0bd0c535ce68c520ae40e415437c78f13a1423d0`; 15/15 tests twice; reproducible archive (`Cuelet-0.1.0-linux-x86_64.tar.gz`, 568,215 bytes, SHA-256 `1215a965b4ae50a22f48afb2fbc1bc3cd3a1011c1f0034ac70cd0c940eb8e73e`) | App-owned virtual source, Cuelet injection, optional physical-source mix, combined physical-output/virtual mode, shortcuts, and cleanup passed within the recorded scope | Broader distro packaging/publication and receiving-client coverage remain |
| Windows | Cuelet 0.1.0.0 x64 rebuilt at Git `c466c01d48e1d04f37e34db338ee9ea3ee8dbf7f` with real installed VB-CABLE endpoints | Independent pair flow, real Cuelet capture, repeated play/stop, local monitoring, cleanup, and relaunch passed within the recorded scope | Production publisher/signing and WACK on that exact signed package remain |

Linux and Windows values above are transcribed from their platform records;
they were not rerun on the macOS host. The Windows source commit is the exact
commit tested on Windows. The only later commit through the macOS-tested Git
commit records the Windows release-validation documentation and does not change
Windows source. The Windows record remains the authority for Windows runtime
identity and measurements.

## Exact macOS closure

The macOS cycle established the production and diagnostic/live driver hashes
as two intentional build variants. The packaged production driver omits
diagnostic telemetry; the separately loaded validation candidate includes it.
They therefore differ by design, while the exact diagnostic candidate was
loaded and validated after a normal restart without restarting `coreaudiod` or
changing system defaults.

| Item | Exact result |
|---|---|
| Validated Git commit | `3084912f994a1d457c67d38d6184933f72ea8160` |
| Host | macOS 26.6.2 build 25G83, arm64 |
| Deployment target | macOS 14.0 |
| Cuelet | 0.1.0 build 1 |
| Driver | 0.1.11 build 12 |
| Package production driver SHA-256 | `f269a9c9b75327431925cfde9b1b0f403a5493f04288d29e91edf8af42d80549` |
| Exact diagnostic/live driver SHA-256 | `9cad2be160bc79de737b71aa4cd8a0e94b8ac241193322a3d7c4a5dcd2a839c8` |
| Installed exact-live hash after restart | `9cad2be160bc79de737b71aa4cd8a0e94b8ac241193322a3d7c4a5dcd2a839c8` |
| Endpoint | `ch.oki.cuelet.virtual-microphone`, 48 kHz, stereo Float32 |
| Deterministic workflow | PASS; 432,000 / 432,000 active payload frames; zero receiver block/event drops, active holes, zero runs, or phase discontinuities |
| Real Cuelet path | 480,256 captured frames over 10.005333 s; 350,047 nonzero; zero receiver drops or active holes |

The production and diagnostic hashes intentionally differ because bounded
telemetry is compiled out of the package artifact. Both were freshly built
from the exact source commit and separately verified. The diagnostic variant
is for validation only; it is not a public package payload.

Immediately after normal package installation, the new on-disk production
driver matched the fresh build while Cuelet correctly reported `Restart
required`; the already-loaded endpoint was not treated as proof of activation.
Installer did not force a restart. After a normal restart, the production
driver enumerated and passed transport. The exact diagnostic candidate was
then installed through the committed developer flow, again reported `Restart
required`, and passed exact-hash loaded validation only after a second normal
restart.

## Capability parity

| Capability | macOS | Linux | Windows |
|---|---|---|---|
| Managed and linked library, metadata, categories, search | Runtime + automated | Runtime + automated | Runtime + automated |
| Play/pause/stop, progress, volume, shortcuts | Runtime + automated | Runtime + automated | Runtime + automated |
| Explicit normal-output playback | Runtime + automated | Runtime + automated | Runtime + automated |
| Cuelet-only virtual-microphone injection | Runtime + measured | Runtime + measured | Runtime + measured |
| Physical-microphone mixing | Not implemented | Runtime + measured | Implemented; not exercised in final Windows cycle because microphone access was disabled |
| Simultaneous normal-output and virtual routing | Not implemented | Runtime + programmatic | Runtime + measured |
| Virtual endpoint lifecycle | Persistent HAL; admin install and normal restart | Transient app-owned PipeWire graph | Persistent separately installed VB-CABLE endpoints |
| Production distribution signing | Missing | Not applicable to a kernel driver | Missing |

The absent macOS physical-microphone mix and simultaneous local-monitor route
are explicit 0.1.0 scope limits, not capabilities inferred from the passing
virtual-only workflow.

## Current automated and package evidence

| Area | Current authoritative result |
|---|---|
| macOS Swift | 118 Debug and 118 Release tests; 2 intentional opt-in skips and 0 failures in each configuration; Debug and Release live-routing tests separately passed |
| macOS driver | 1,252,215 core, 15,678 diagnostic contract, 4,360 production contract, 50 replay, and 1,396 telemetry assertions; three analyzer tests |
| macOS robustness | 100,000-iteration stress, ASan/UBSan, standalone UBSan, TSan, and three Clang analyzer translation units passed |
| macOS local package | App/driver identity, payload, upgrade/downgrade guards, icon, license, developer-path scan, and release hygiene passed; local-only warning present; unsigned public-looking output refused |
| Linux release | Fresh x86_64 Release build twice; 15/15 tests each; archive content, metadata, dependency, icon, license, path, and AppStream-pedantic checks passed; archives were byte-identical |
| Windows release | x64 Rebuild, aggregate core suite, metadata/icon check, and actual unsigned MSIX content audit passed |

## Release conclusion

No macOS, Linux, or Windows source/functional blocker recorded here remains
before tagging Cuelet 0.1.0. Publication work is separate from tagging the
validated source:

- macOS still needs production Developer ID Application/Installer signing,
  notarization, stapling, and validation of that exact public artifact;
- Windows still needs the production publisher identity, signing, and WACK on
  that exact production-signed MSIX; and
- final hosting, release notes, checksums, and publication should use only the
  subsequently produced signed/notarized artifacts.

Broader macOS sleep/wake and receiving-application coverage, Linux receiver
coverage beyond the recorded PipeWire client, and full accessibility audits
remain compatibility or quality follow-up. They are not claimed as completed
0.1.0 functionality.
