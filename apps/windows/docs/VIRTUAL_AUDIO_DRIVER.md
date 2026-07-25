# Cuelet Virtual Audio Driver

## Repository state

Before this implementation, the repository was in state **D: no Cuelet driver
exists**. It contained app-side AudioGraph routing, an endpoint selector, and a
developer design guide, but no `.inf`, `.inx`, `.sys`, `.cat`, WDK project,
installer helper, certificate, or driver package.

The tree now contains the driver source architecture, packaging integration,
and a repeatable local Debug validation workflow:

```text
apps/windows/
  Cuelet.VirtualAudio.Driver/
    Cuelet.VirtualAudio.Driver.vcxproj
    UPSTREAM_SYSVAD_REVISION.txt
    prepare-driver-source.ps1
    overlay/EndpointsCommon/CueletAudioBridge.*
  Cuelet.VirtualAudio.Installer/
  Cuelet.VirtualAudio.FlowTest/
  Cuelet.VirtualAudio.Shared/
  scripts/
    build-virtual-audio-driver.ps1
    package-virtual-audio-driver.ps1
    enable-virtual-audio-driver-testing.ps1
    install-virtual-audio-driver.ps1
    verify-virtual-audio-flow.ps1
    uninstall-virtual-audio-driver.ps1
```

Local Debug builds produce a fully test-signed `.sys`, `.inf`, and `.cat`
package. Build products and development certificates are ignored and are never
source-controlled. Do not describe this as publicly distributable or
production-ready: public delivery still requires Microsoft-compatible
production signing and the release validation below.

## Architecture

The build pins Microsoft Windows Driver Samples SysVAD commit
`2ee527bfeb0aeb6be11f0a8b6dce4011b358ce89`. The preparation script uses a
sparse checkout and strict source transforms, so an upstream change fails
instead of silently producing a different driver.

Only two TabletAudioSample miniports are exposed:

- render: `Cuelet Virtual Microphone Input`
- capture: `Cuelet Virtual Microphone`

Both endpoints are 48 kHz, 16-bit, stereo PCM. The upstream sample's capture
sine generator is replaced by a 512 KiB nonpaged circular PCM bridge:

1. the render stream publishes each processed DMA span;
2. each capture stream has an independent monotonic read cursor;
3. lagging readers jump to the newest request-sized span to bound latency;
4. underruns and format mismatches produce silence;
5. capture reader state is revoked when its WaveRT stream closes.

This is a real connection between the two stream paths, not two disconnected
renamed sample endpoints. It still requires WDK compilation and kernel/audio
validation before it can be trusted.

Stable ownership/pairing values are shared with the app and helper:

- provider/manufacturer: `Cuelet`
- hardware ID: `ROOT\CUELETVIRTUALAUDIO`
- custom pairing property:
  `{1A7B44F5-2C93-48F5-A18B-46399D69E13F}, 2`
- pairing value: `{8B9D3BB9-8C4E-4EF5-94D5-4BE741D4D892}`
- common device container: required during endpoint verification

Display names improve usability but are not the ownership boundary.

## Toolchain and build

Required:

- Visual Studio C++ driver development workload
- Windows SDK and WDK 10.0.26100
- x64 or ARM64 kernel-mode build tools
- Inf2Cat and SignTool

Prepare the pinned source without compiling:

```powershell
powershell -ExecutionPolicy Bypass -File `
  .\apps\windows\scripts\build-virtual-audio-driver.ps1 `
  -Configuration Debug -Architecture x64 -PrepareOnly
```

Build the real driver:

```powershell
powershell -ExecutionPolicy Bypass -File `
  .\apps\windows\scripts\build-virtual-audio-driver.ps1 `
  -Configuration Release -Architecture x64
```

Run the WDK Code Analysis for Drivers pass before packaging a runtime
candidate:

```powershell
powershell -ExecutionPolicy Bypass -File `
  .\apps\windows\scripts\build-virtual-audio-driver.ps1 `
  -Configuration Debug -Architecture x64 -Rebuild -Analyze
```

The script disables automatic build signing. It never creates or trusts a
certificate, enables test-signing, changes Secure Boot, or disables signature
enforcement.

## Production signing

Create a release submission artifact:

```powershell
powershell -ExecutionPolicy Bypass -File `
  .\apps\windows\scripts\package-virtual-audio-driver.ps1 `
  -Configuration Release -Architecture x64 -PrepareSubmission
```

That artifact is explicitly unsigned and must never be bundled with the app.
Submit the exact package through the Microsoft Hardware Dev Center
attestation/HLK signing path appropriate to the distribution. After receiving
the signed catalog for the exact package, create the bundle with:

```powershell
powershell -ExecutionPolicy Bypass -File `
  .\apps\windows\scripts\package-virtual-audio-driver.ps1 `
  -Configuration Release -Architecture x64 `
  -SignedCatalogPath <path-to-returned-CueletVirtualAudio.cat>
```

Release packaging runs kernel-policy signature verification and deletes the
candidate package on failure. The Release installer independently verifies the
catalog through Windows driver policy before staging it.

No production certificate, Hardware Dev Center account, signed catalog, or
submission result is stored in this repository.

## Development-only package

The preserved `20.37.42.726` crash package is retired evidence. The installer
rejects that exact version even when its Debug test-package option is enabled.
Never use the conventional preserved package as a test candidate; use a new,
separately versioned and hash-locked directory.

Debug packaging requires the explicit `-AllowTestPackage` flag and an existing
test code-signing certificate in `Cert:\CurrentUser\My`. The script signs both
the driver image and generated catalog, then exports only the public certificate
next to the package output. It does not generate a certificate, install a trust
root, or turn on Windows test-signing:

```powershell
$certificate = New-SelfSignedCertificate `
  -Type CodeSigningCert `
  -Subject "CN=Cuelet Virtual Audio Development" `
  -CertStoreLocation "Cert:\CurrentUser\My" `
  -KeyAlgorithm RSA -KeyLength 3072 -HashAlgorithm SHA256 `
  -NotAfter (Get-Date).AddYears(2) `
  -KeyExportPolicy NonExportable

powershell -ExecutionPolicy Bypass -File `
  .\apps\windows\scripts\package-virtual-audio-driver.ps1 `
  -Configuration Debug -Architecture x64 `
  -AllowTestPackage -TestCertificateThumbprint $certificate.Thumbprint
```

On a dedicated development machine with Secure Boot disabled, run the one-time
setup from an elevated PowerShell and restart Windows:

```powershell
powershell -ExecutionPolicy Bypass -File `
  .\apps\windows\scripts\enable-virtual-audio-driver-testing.ps1
Restart-Computer
```

This trusts only the exported Cuelet development certificate and enables
Windows `TESTSIGNING` for the next boot. Windows displays Test Mode while this
development policy is active. It is not an end-user deployment procedure.

The Debug app enables its developer driver path only when
`CUELET_ALLOW_TEST_DRIVER=1` is present, and then passes the explicit
`--allow-test-package` argument to the elevated Debug helper. The helper option
is compiled out of Release. The explicit argument is the UAC-safe boundary
because process-local environment changes are not reliably inherited through
the `runas` launch.

After restarting, either invoke the helper workflow:

```powershell
powershell -ExecutionPolicy Bypass -File `
  .\apps\windows\scripts\install-virtual-audio-driver.ps1 `
  -Configuration Debug -AllowTestPackage
```

or exercise the actual in-app action:

```powershell
powershell -ExecutionPolicy Bypass -File `
  .\apps\windows\scripts\run-windows.ps1 `
  -Configuration Debug -NoBuild -AllowTestDriver
```

Open **Audio Setup** and choose **Install Virtual Microphone**. The Debug-only
flag is inherited by the app and helper; Release builds never honor it.

## Elevated helper

`Cuelet.VirtualAudio.Installer.exe` supports:

```text
install
repair
uninstall
status --json
```

The WinUI process launches mutating commands with the standard `runas` verb and
waits off the UI thread. The helper:

- requires elevation for changes;
- accepts no arbitrary INF path;
- reads only its adjacent `DriverPackage\CueletVirtualAudio.inf`;
- validates Cuelet provider, hardware ID, endpoint declarations, architecture,
  catalog, and Windows build;
- stages with `SetupCopyOEMInf`;
- creates only `ROOT\CUELETVIRTUALAUDIO`;
- updates the root device without forcing a downgrade;
- waits for both active endpoints;
- verifies provider, pairing property, container, direction, and exact endpoint
  role;
- returns structured JSON through a constrained result path;
- rolls back a non-reboot partial installation;
- removes only exact Cuelet hardware/manufacturer devices and their `oem*.inf`
  identities.

Status is not considered successful from the process exit code alone.

## Local render-to-capture verification

`Cuelet.VirtualAudio.FlowTest.exe` is an independent WASAPI client. It requires
exactly one active endpoint with each Cuelet name, renders a known 997 Hz stereo
signal, captures simultaneously, and measures RMS and tone purity over
one-second windows. Run the status and flow checks together with:

```powershell
powershell -ExecutionPolicy Bypass -File `
  .\apps\windows\scripts\verify-virtual-audio-flow.ps1 `
  -Configuration Debug -NoBuild
```

A successful result includes both endpoint IDs and
`Render-to-capture flow: PASS`.

## Required release validation

Complete all of the following on clean x64 and, if distributed, ARM64 systems:

1. Run InfVerif, CodeQL/Static Driver Verifier, driver signing verification, and
   targeted Driver Verifier appropriate to an AVStream/PortCls driver on a
   disposable test system. Never enable Driver Verifier on the daily-use
   development laptop.
2. Install with Secure Boot enabled and test-signing disabled.
3. Confirm Windows accepts the package and record the published OEM INF.
4. Confirm exactly the two expected endpoints appear with the stable properties.
5. Render a known PCM sequence into the input and capture it simultaneously from
   at least Discord/OBS and an independent WASAPI test client.
6. Compare captured samples, channel order, latency, underrun behavior, multiple
   readers, suspend/resume, format negotiation, and long-run clock behavior.
7. Test install, already installed, repair, upgrade, downgrade refusal, UAC
   cancellation, delayed endpoint creation, reboot-required, rollback, and
   uninstall.
8. Confirm unrelated virtual-audio packages are untouched.

Until these checks pass, endpoint detection and audio flow are **not verified**.
