# Cuelet virtual microphone design

## Current status

Cuelet does **not** install or expose a Windows capture endpoint. The current app can send soundboard audio to a selected secondary render endpoint and can forward a physical microphone to that same endpoint. This works with an already installed virtual cable whose paired capture endpoint can be selected in Discord, a game, a call, or recording software.

The settings status must remain `Virtual microphone: Not installed` unless Windows enumerates a real Cuelet capture endpoint. A normal speaker/render endpoint is never described as a microphone.

## App-side routing (implemented)

- `Playback output` selects the local render device.
- `Broadcast output` selects a second render device or disables broadcasting.
- Each sound uses the existing reliable `MediaPlayer` path locally and a second `MediaPlayer` stream for broadcast when both outputs are enabled.
- `Monitor locally` can disable the local stream.
- An `AudioGraph` forwards the selected physical capture device to the broadcast render device when microphone mixing is enabled. Windows' audio engine mixes that stream with Cuelet's broadcast sound streams at the selected endpoint.
- Broadcast, microphone, and soundboard gains are persisted separately.
- Device-open failures are surfaced in the settings `InfoBar`; they are not treated as virtual-microphone success.

The next app-side iteration should add `DeviceWatcher` recovery, clock-drift measurement for long simultaneous local/broadcast playback, and integration tests with a loopback endpoint.

## Native driver architecture (not implemented)

The intended layout is:

```text
apps/windows/
  Cuelet.WinUI/
  Cuelet.Audio/
  Cuelet.VirtualAudio.Driver/
  Cuelet.VirtualAudio.Bridge/
  docs/VIRTUAL_MICROPHONE.md
```

The driver should be derived deliberately from Microsoft's SysVAD virtual audio driver sample, not copied wholesale. It must expose a paired topology:

- render endpoint: `Cuelet Virtual Mic Input`
- capture endpoint: `Cuelet Virtual Microphone`

`Cuelet.VirtualAudio.Bridge` will accept the app's mixed PCM stream and feed the render side. The capture side must deliver the same frames, formats, timestamps, discontinuity flags, and silence behavior to Windows audio clients. The bridge protocol needs versioning, bounded shared-memory buffers, explicit underrun/overrun counters, process-lifetime recovery, and access control that does not allow arbitrary untrusted processes to inject audio.

## Driver delivery requirements

1. Build x64 first with a matching Visual Studio, Windows SDK, and WDK toolchain.
2. Keep driver installation separate from normal app launch. Show an administrator confirmation and never install silently.
3. For local development, document Windows test-signing mode and use a locally generated certificate outside the repository.
4. Production distribution requires Microsoft-compatible release signing/attestation and a signed catalog. No development certificate, private key, generated package, or build output may be committed.
5. Provide a signed installer plus a clean uninstall that removes both endpoints, services, bridge registration, and driver package.
6. Verify in Windows Sound settings and at least two independent capture clients that the capture endpoint exists and receives the app's audio before changing product status to `Installed` or `Connected`.

Microsoft's maintained references are the [SysVAD sample](https://github.com/microsoft/Windows-driver-samples/tree/main/audio/sysvad), the [SysVAD build/install sample page](https://learn.microsoft.com/en-us/samples/microsoft/windows-driver-samples/sysvad-virtual-audio-device-driver-sample/), and the [Universal audio driver guidance](https://learn.microsoft.com/en-us/windows-hardware/drivers/audio/audio-universal-drivers).

## Why there is no driver scaffold in this tree yet

The current development machine does not have the WDK driver targets or Windows driver samples installed. Creating an unbuildable project would give false confidence and would not produce a selectable endpoint. Add the driver and bridge projects only after the WDK is installed, the selected SysVAD revision is pinned, and a minimal paired render/capture endpoint can be built and manually verified.
