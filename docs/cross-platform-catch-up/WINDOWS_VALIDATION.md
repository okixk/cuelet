# Cuelet 0.1.0 Windows Release Validation

## Scope and identity

This record covers the final Windows pre-tag validation performed on
2026-08-21. The tested source commit was
`c466c01d48e1d04f37e34db338ee9ea3ee8dbf7f` on
`feat/linux-parity-catch-up`. The working tree was clean when that commit was
built and tested.

The test environment was Microsoft Windows 11 Home build 26100, x64. The
Release configuration was rebuilt for x64 with MSBuild's `Rebuild` target.
No macOS or Linux implementation code was changed.

| Item | Tested value |
|---|---|
| Cuelet product version | `0.1.0` |
| PE file version | `0.1.0.0` |
| MSIX version | `0.1.0.0` |
| Architecture | x64 |
| Declared minimum Windows version | Windows 10 1809 (`10.0.17763.0`) |
| Release executable | `apps/windows/x64/Release/Cuelet.WinUI/Cuelet.exe` |
| Release executable SHA-256 | `432E0A450FAE6026C6245BD559CDCCAC269ADEB9B2A59457E19401683F70099A` |
| Unsigned MSIX SHA-256 | `8B0E9FBE5EF7A15EA704ECEF66DD68153D88F9BF7B8C387DDE480B2432CED79E` |
| MSIX size | 1,223,953 bytes |

## Automated build and package results

- Release x64 `Rebuild` plus unsigned MSIX generation: 1/1 passed.
- `Cuelet.Core.Tests.exe`: 1/1 test executable passed. This suite includes the
  Windows About identity/license assertions and the endpoint classification,
  VB-CABLE pairing, and auxiliary-endpoint regression checks. The executable
  reports one aggregate suite result rather than individual case counts.
- Windows release metadata and icon check: 1/1 passed.
- Actual MSIX content audit: 1/1 passed.

The generated manifest identifies x64 version `0.1.0.0` and Windows 10 1809 as
the minimum. The package audit found no Cuelet SysVAD driver/installer, driver
package, PDB/debug content, source/test files, private key/certificate files,
signature, or developer-machine path. No generated EXE, MSIX, PDB, AppPackages,
or x64 output is tracked by Git.

## License packaging

The complete repository `LICENSE` is packaged as `LICENSE` at the MSIX root.
MSBuild sources that item directly from the repository root; there is no second
maintained copy. The repository and packaged files were both 34,524 bytes and
had SHA-256
`8D56B405468AAD11F87AB5763F901E276E08D9646FF5C8481B1762B6B789E9ED`.
The archive audit therefore proved byte-for-byte equality. Future Release
packaging invokes the same audit and fails if `LICENSE` is missing or differs.

## Installed VB-CABLE validation

VB-CABLE was already installed and was not reinstalled, repaired, or modified.
The observed active endpoints were:

- render: `CABLE Input (VB-Audio Virtual Cable)`;
- capture: `CABLE Output (VB-Audio Virtual Cable)`;
- auxiliary render endpoint: `CABLE In 16ch (VB-Audio Virtual Cable)`.

The exact rebuilt Release app was registered as an unsigned loose package for
local Developer Mode execution. This supplied package identity without signing
or generating a certificate. The registration was removed after the run.

At runtime, the Release UI reported `VB-CABLE virtual microphone · Connected`
and described a matching CABLE Input/CABLE Output pair. Its voice-chat output
choices were only `Off` and the standard `CABLE Input`; `CABLE In 16ch` was not
offered as a voice-chat output. The paired capture selection was the standard
`CABLE Output`.

### Independent pair flow

The Release WASAPI verifier rendered a 997 Hz tone to the standard input and
captured the standard output:

| Measurement | Result |
|---|---:|
| Captured frames | 191,040 |
| Discontinuities | 1 |
| RMS | 0.2475 |
| 997 Hz RMS | 0.2475 |
| Tone purity | 1.0000 |
| Observed signal onset | 80.0 ms |
| Result | PASS |

### Real Cuelet playback

A generated four-second WAV fixture was played by the current Release app and
captured from the real `CABLE Output`. The fixture and all captures remained in
temporary validation storage and were not added to the repository.

| Measurement | Result |
|---|---:|
| Cuelet play command | exit 0 |
| Captured frames | 383,040 |
| Capture peak | 0.34929657 |
| Capture RMS | 0.01060267 |
| Discontinuities | 1 |
| Release UI responsive after playback | yes |

The source/capture envelope correlation was 0.99837, aligned full-band gain was
0.99717, no clipped samples or click candidates were found, and channel-balance
error was 0 dB. The stricter comparison helper returned nonzero because it
found one zero-filled 10 ms quantum and a 500 ms onset difference from its
configured preroll. That helper result is not claimed as a pass; the direct
non-silent render-to-capture result above is the accepted runtime evidence.

### Repeated play and stop

Two play commands and two Stop All commands each returned exit 0. A ten-second
capture contained two distinct active runs at 1,300–2,160 ms and 3,500–6,500 ms.
It contained 479,520 frames with peak 0.02120972, RMS 0.00928080, and one
discontinuity. Cuelet remained responsive.

### Physical microphone and local monitoring

Physical-microphone mixing was not tested. The Release UI reported that Windows
microphone access was disabled. The privacy setting was not changed, so no
microphone-mixing pass is claimed.

Local monitoring was tested without changing the system default device. During
one Cuelet play, simultaneous capture measured:

| Route | Frames | Peak | RMS | Discontinuities |
|---|---:|---:|---:|---:|
| `CABLE Output` | 383,520 | 0.02120972 | 0.01051370 | 1 |
| `Speakers (Realtek(R) Audio)` loopback | 382,976 | 0.00952148 | 0.00411476 | 1 |

Both routes were non-silent and the Release UI remained responsive.

### Cleanup and relaunch

Cuelet closed normally. A three-second post-close capture contained 143,040
frames with peak 0 and RMS 0. Relaunch created a responsive window, again
reported VB-CABLE Connected, retained the exact standard render/capture pair,
continued to exclude the 16-channel auxiliary endpoint, and closed normally.
The temporary loose-package registration was then removed. VB-CABLE remained
installed and was not altered.

## WACK and signing boundary

Windows App Certification Kit `appcert.exe` version `10.0.28000.2526` is
installed. The machine contains an older WACK report dated 2026-08-12, but it is
not for the tested Git HEAD or this MSIX hash and is not credited to this
candidate. No current-HEAD WACK pass is claimed.

The public candidate remains unsigned and retains the guarded development
publisher placeholder. No temporary certificate was created or retained for
this validation. WACK must be rerun on the actual production candidate after
the Store identity or trusted production certificate is available and the
package has been signed through the existing production-signing workflow.

## Conclusion

No Windows source or functional blocker was found for tagging Cuelet 0.1.0.
The remaining Windows publication blockers are production publisher identity,
production signing, and WACK on that exact production-signed candidate.
