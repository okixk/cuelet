# Cuelet macOS 0.1.0 validation

Validation date: 2026-08-21.

This is the authoritative macOS release-candidate record for Cuelet 0.1.0. It
supersedes the earlier 0.1.1–0.1.7 investigation notes and obsolete test
counts. Historical details remain available in Git history. No generated app,
package, driver, recording, raw telemetry, or machine-local validation path is
tracked by this record.

## Exact candidate identity

| Item | Validated value |
|---|---|
| Git commit | `3084912f994a1d457c67d38d6184933f72ea8160` |
| Git branch | `feat/linux-parity-catch-up` |
| Source state before build | clean; local HEAD equaled `github/feat/linux-parity-catch-up` |
| macOS | 26.6.2, build 25G83 |
| Architecture | arm64 |
| Minimum macOS target | 14.0 |
| Cuelet | 0.1.0 build 1 |
| Cuelet executable SHA-256 | `c8f0c1a6a167d6fd7e1d8c982bfb4b7c5e0a0374ec4403ea2688058eeb87e5dc` |
| Cuelet Virtual Microphone | 0.1.11 build 12 |
| Local-package production driver SHA-256 | `f269a9c9b75327431925cfde9b1b0f403a5493f04288d29e91edf8af42d80549` |
| Exact diagnostic/live candidate SHA-256 | `9cad2be160bc79de737b71aa4cd8a0e94b8ac241193322a3d7c4a5dcd2a839c8` |
| Local package SHA-256 | `fe384be20f29caca03536aab27b23dfe0d4d44edcf82c50dea7ed476026bf154` |

There are intentionally two driver executable hashes for the same transport
source and bundle version. The local release package contains the
diagnostics-disabled production HAL. The committed live diagnostic workflow
pins the diagnostics-enabled HAL, whose bounded telemetry is compiled out of
the production artifact. Neither is a public distribution artifact; both were
built from the exact commit above.

## Clean build and package result

The committed macOS scripts built a fresh Release `Cuelet.app`, Release virtual
audio driver, and `Cuelet-0.1.0-local.pkg` after normal generated output was
cleared and SwiftPM used a fresh scratch tree.

The package builder proved that:

- the app, embedded driver, and standalone driver identities and versions were
  consistent;
- the embedded and standalone production driver executables were byte-identical;
- the app and driver were arm64 and structurally code-signature-valid with
  local ad-hoc signatures;
- the app contained the compiled native icon and the exact repository license;
- the package payload contained no tests, tools, recordings, source files, or
  developer-specific paths;
- clean install, same-version repair, older-driver upgrade, newer-driver
  downgrade rejection, foreign exact-path rejection, and unrelated HAL bundle
  preservation passed;
- Installer does not restart Core Audio and does not force restart, logout, or
  shutdown; and
- Welcome, Read Me, and Summary all contained
  `LOCAL TEST PACKAGE — NOT FOR PUBLIC DISTRIBUTION`.

The final local package is unsigned and no `Cuelet-0.1.0.pkg` public-looking
artifact was emitted. A release-mode invocation without both Developer ID
identity variables failed before packaging and reported that no unsigned or
ad-hoc fallback was used.

## Installation and restart behavior

Before replacement, the installed driver reported 0.1.11 build 12 with
executable SHA-256
`f269a9c9b75327431925cfde9b1b0f403a5493f04288d29e91edf8af42d80549`.
The local package was installed with Apple's normal Installer flow. Immediately
after installation, both the installed driver and installed application were
byte-identical to the freshly built production artifacts.

Before the first normal restart:

- Cuelet launched and normal System Output playback entered the playing state;
- Cuelet reported `Restart required` for the driver;
- the already-enumerated endpoint was not presented as the newly activated
  on-disk driver;
- Installer had completed without forcing a restart; and
- Cuelet remained running without a crash or user-facing runtime warning.

After a normal restart, the production driver was enumerated by Core Audio with
its stable device UID, stereo input and output streams, and 48 kHz current
sample rate. Cuelet reported `Driver ready — selected`. The production HAL
also passed deterministic transport and real Cuelet-playback captures before
the diagnostic workflow cycle.

To close the audit's exact workflow-hash requirement, the committed developer
installer then installed the diagnostics-enabled candidate. Before the second
normal restart, the built and installed hashes both equaled
`9cad2be160bc79de737b71aa4cd8a0e94b8ac241193322a3d7c4a5dcd2a839c8`, while
Cuelet again reported `Restart required` despite the old production endpoint
remaining visible. No `coreaudiod` restart or Core Audio manipulation occurred.

The second normal restart occurred at 16:54 local time. After that restart:

- installed version/build remained 0.1.11/12;
- installed SHA-256 exactly equaled the diagnostic candidate SHA-256;
- Core Audio enumerated plug-in `ch.oki.cuelet.virtual-microphone.driver`,
  device UID `ch.oki.cuelet.virtual-microphone`, input stream `Cuelet Loopback
  Input`, and output stream `Cuelet Injection Output`;
- the endpoint exposed two input and two output channels at 48 kHz; and
- Cuelet reported `Driver ready`, then `Driver ready — selected` after the
  virtual output was selected.

The post-restart enumeration, exact installed hash, and live diagnostic build
identity establish that Core Audio loaded the exact current-HEAD candidate.

## Committed live transport and continuity workflow

`apps/macos/scripts/run-virtual-audio-live-diagnostics.sh` completed with
`status=PASS`, zero workflow failures, exact installed/candidate hash equality,
and complete event-level diagnosis.

| Measurement | Result |
|---|---|
| Endpoint | `ch.oki.cuelet.virtual-microphone` |
| Format | 48,000 Hz, 2-channel Float32 |
| Injector | 937 callbacks, 479,744 frames, 9.994666667 s, 0 unsupported buffers |
| Receiver | 1,313 callbacks, 672,256 captured/written frames, 14.005333 s |
| Receiver queue drops | 0 block drops; 0 event drops |
| Driver writes | 672,256 input/stored/published frames; 0 publication failures |
| Driver reads | 671,744 valid frames; one expected 512-frame pre-calibration startup block |
| Expected/received active payload | 432,000 / 432,000 nonzero frames |
| Frequencies | 996.999247 Hz left; 1498.998931 Hz right; validation passed |
| Peak | 0.25 left/right; validation passed |
| Continuity | 0 active zero runs, 0 candidate holes, 0 phase discontinuities |
| Lifecycle | 1 balanced StartIO/StopIO pair; 2 resets; 2 generation changes |

The diagnostic event ring reported bounded-history overwrites during the run.
That counter describes old telemetry records being replaced in the 8,192-event
store; it is not an audio or receiver drop. Receiver block and event drop
counters were both zero, and the captured payload had no active holes.

## Genuine Cuelet application routing

With the exact diagnostic candidate loaded, Cuelet selected `Cuelet Virtual
Microphone` and displayed `Driver ready — selected`. A real Cuelet library item
was played while the committed HAL receiver captured the named virtual input.

| Measurement | Result |
|---|---|
| Capture | 480,256 stereo frames at 48 kHz over 10.005333 s |
| Real Cuelet payload | 350,047 nonzero frames |
| Receiver drops | 0 block drops; 0 event drops |
| Active-region integrity | 0 active zero runs; 0 candidate holes |
| Driver writes | 480,256 stored and published frames; 0 publication failures |
| Driver reads | 479,744 valid frames; one expected 512-frame startup block |
| UI evidence | playback state observed while the virtual output was selected |

The deterministic 997/1499 Hz criteria belong to the generated workflow, not
to this arbitrary real sound. This application-path capture is used only to
prove nonzero end-to-end Cuelet playback through the virtual input.

Cuelet was then returned to System Output and a separate real library item
entered the playing state. The Debug and Release live routing tests also
confirmed System Output and every explicit output UID without changing the
macOS system default.

## Current automated results

| Suite | Current result |
|---|---|
| Swift Debug | 118 tests, 2 intentional opt-in skips, 0 failures |
| Swift Release | 118 tests, 2 intentional opt-in skips, 0 failures |
| Swift live routing, Debug | 1 test, 0 failures |
| Swift live routing, Release | 1 test, 0 failures |
| Swift Release performance | 1 test, 0 failures; 250/1,000-file scan and search fixture |
| Driver timeline core | 1,252,215 assertions |
| Diagnostic driver contract | 15,678 assertions |
| Diagnostics-disabled contract | 4,360 assertions |
| Luna replay | 50 assertions |
| Telemetry store | 1,396 assertions |
| Capture/receiver analyzers | 3 tests total |
| Driver stress | 100,000 iterations, pass |
| ASan + UBSan | core, diagnostic contract, and telemetry tests pass |
| Standalone UBSan | core, diagnostic contract, and telemetry tests pass |
| TSan | core, diagnostic contract, and telemetry tests pass |
| Clang static analyzer | all 3 driver translation units pass |
| Production bundle verifier | structure, identity, signature, destination guard, and smoke pass |
| Release/package validation | all package policy, icon, license, path, and hygiene checks pass |

The performance fixture recorded approximately 30.52 ms for 250-file scan,
116.05 ms for 1,000-file scan, 224.34 ms for 1,000-file metadata reload, and
4.74 ms for 1,000-file search on this host. These timings are observational,
not release thresholds.

## Release conclusion and remaining boundaries

No macOS source or scoped 0.1.0 functional blocker remains before tagging.
The exact-HEAD virtual transport, Cuelet application route, restart-state
handling, normal playback, current automated suites, and local Installer
contract all passed.

The following are separate distribution or product-scope boundaries:

- production Developer ID Application and Installer signing are not configured;
- notarization, stapling, and public Gatekeeper assessment remain required;
- the checked package is explicitly local-only and must not be published;
- macOS 0.1.0 does not implement physical-microphone mixing or simultaneous
  local-speaker monitoring while routing to the virtual microphone; and
- sleep/wake and a broad third-party receiving-application matrix remain future
  compatibility coverage, not claimed release functionality.
