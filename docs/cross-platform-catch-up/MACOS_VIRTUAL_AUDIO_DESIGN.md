# Cuelet macOS virtual-audio design

Status: explicit stable-UID output routing and the first Cuelet-owned Audio Server Driver Plug-in are implemented. The driver is installed and loaded on the validation Mac after reboot, and the live device/transport has been exercised. The current arm64 development build still has a live receiver-continuity limitation documented in `MACOS_VALIDATION.md`.

## Decisions

Cuelet supports one honest playback destination at a time:

1. System Output;
2. an explicit live physical output selected by stable Core Audio UID; or
3. the Cuelet-owned virtual device output selected by stable UID after the driver is loaded.

Cuelet never changes the macOS system default. Speakers-plus-virtual output is not implied by selecting the virtual device.

The owned virtual microphone uses an **Audio Server Driver Plug-in**. Apple's [Creating an Audio Server Driver Plug-in](https://developer.apple.com/documentation/coreaudio/creating-an-audio-server-driver-plug-in) sample defines the correct software-only virtual-device model and HAL bundle path. Apple's [Creating an audio device driver](https://developer.apple.com/documentation/audiodriverkit/creating-an-audio-device-driver) documentation says AudioDriverKit supports physical devices and recommends an Audio Server Driver Plug-in for virtual devices. Cuelet therefore has no DriverKit extension, kernel extension, deprecated kernel driver, SIP change, or DriverKit entitlement.

## Existing explicit output routing

The app enumerates alive output devices with streams and stable `kAudioDevicePropertyDeviceUID` values. `AVAudioPlayer.currentDevice` is set before playback; `nil` follows System Output. Active and paused players change route transactionally and roll back if any player rejects the UID. The installed Cuelet device is selected by this stable UID in the live Release app.

Device-list, default-output, liveness, name, and output-stream changes are observed. Stop and Wait is the default device-loss policy; temporary System Output is opt-in and preserves the missing exact UID. A similarly named device is never substituted.

This architecture remains appropriate for the new driver: Cuelet injects audio through the virtual device's ordinary output stream without an engine rewrite or IPC.

## Cuelet-owned device

Stable identity:

```text
Bundle ID:    ch.oki.cuelet.virtual-microphone.driver
Device name:  Cuelet Virtual Microphone
Manufacturer: Cuelet
Device UID:   ch.oki.cuelet.virtual-microphone
Model UID:    ch.oki.cuelet.virtual-microphone.model
Version:      0.1.0 (1)
```

Object graph:

```text
AudioPlugIn
└── AudioDevice
    ├── input stream  — stereo interleaved Float32 LPCM
    ├── output stream — stereo interleaved Float32 LPCM
    ├── functional input volume/mute
    └── functional output volume/mute
```

Both 44.1 and 48 kHz are explicit supported modes. Format changes use the host configuration-change handshake; the real-time callback does no conversion.

## Loopback and real-time policy

Core Audio's mixed output frames enter a preallocated 16,384-frame C11 atomic ring. Input clients have independent fixed preallocated cursors. The output callback publishes absolute frame sequences; a reader accepts a frame only when its sequence remains stable around both atomic channel loads.

Underruns produce silence. A lagging reader jumps to the newest request-sized span on overrun. First start, last stop, explicit reset, and sample-rate change clear all published sequences. New readers begin at the current write head and cannot replay old sound.

No Swift, Objective-C, allocation, filesystem, network, logging, blocking synchronization, semaphore, condition, IPC, or HAL client call occurs in `DoIOOperation`. `GetZeroTimeStamp` is lock-free. Client registration and non-I/O property changes may use bounded synchronization outside the real-time callback.

## Driver process boundary

The plug-in does not read Cuelet Application Support, the sound library, user documents, or arbitrary files. It has no UI and no networking. It never captures a physical microphone. Its only audio source is the ordinary output stream Core Audio provides.

No IPC is required for the first version. Later control or telemetry must justify a small versioned channel and must still keep IPC outside real-time callbacks.

## Installation and application state

Development install and uninstall remain explicit Terminal workflows under `apps/macos/scripts/`. The destination is exactly `/Library/Audio/Plug-Ins/HAL/CueletVirtualAudio.driver`. Scripts validate Cuelet identity, never touch another vendor, never disable SIP, never change defaults, never kill `coreaudiod`, and require a manual restart. The current validation observed the existing installed bundle and did not reinstall or reboot it.

The injectable app service distinguishes prepared files from runtime readiness. Ready requires both a live input and output with the stable UID. An installed bundle with no live device is Restart required only when its plist was modified since boot; otherwise it is Unavailable. Version, update, installation, and routing failures are separate states.

## Physical microphone mixing

Physical microphone mixing remains out of scope. A future app-side mixer must request permission only after explicit activation, show a persistent indicator, keep independent gains/meters, refuse virtual-input feedback loops, stop on device/permission loss, and never record or transmit the microphone itself. The driver will not open the physical microphone.

## Speakers plus virtual microphone

Simultaneous local speakers and virtual output is not implemented. It needs either two synchronized render paths or an owned temporary aggregate/multi-output design with drift, latency, removal, crash-cleanup, and hour-long soak validation. The first driver intentionally preserves a single destination.

## Production boundary

The current arm64 bundle is locally ad-hoc signed. Production still requires an appropriate Developer ID distribution decision, consistent nested signing, Hardened Runtime review where applicable, notarization, stapling, Gatekeeper tests, updater/rollback design, and a supported-macOS receiving-application matrix. No private signing material belongs in the repository.

See `MACOS_VIRTUAL_AUDIO_DRIVER.md` and `MACOS_VALIDATION.md` for the object/property contract, ring algorithm, stable identifiers, platform mapping, privacy model, measured runtime results, and remaining limitations.
