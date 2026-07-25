# Cuelet Virtual Microphone

Cuelet distinguishes three independent audio roles:

- **Physical microphone** — a real capture endpoint such as a USB microphone,
  headset microphone, or microphone array.
- **Local playback** — headphones, speakers, or another render endpoint used by
  the person running Cuelet.
- **Virtual microphone route** — a paired virtual render and capture endpoint.
  Cuelet renders its soundboard/microphone mix to the first; Discord, games,
  OBS, and call applications record from the second.

Ordinary speakers plus an ordinary microphone are not a virtual pair. The
normal selector accepts only Cuelet-owned endpoints identified by provider,
hardware ID, pairing property, and common container, or an explicitly supported
third-party cable family. Arbitrary choices are available only in the clearly
labeled Advanced manual pairing mode.

## End-user flow

When a signed driver package is bundled, Audio Setup presents **Install Virtual
Microphone**, explains the UAC request and driver change, launches the dedicated
helper with the Windows `runas` verb, waits without blocking WinUI, verifies the
complete pair, refreshes endpoint enumeration, and selects:

- broadcast render: `Cuelet Virtual Microphone Input`
- recording endpoint shown to other apps: `Cuelet Virtual Microphone`
- physical input: the default communications microphone
- physical microphone mixing: enabled

Local playback and monitoring are preserved. Cuelet does not change the global
Windows default microphone.

If installation is unavailable or fails, local playback remains usable.
Troubleshooting is then exposed through **View Diagnostic Details**; users are
not directed through Device Manager or PnPUtil during the normal setup.

## Current delivery boundary

Driver source preparation, the render-to-capture bridge, test-signed Debug
packaging, the elevated helper, app integration, endpoint classification,
status UI, and an independent WASAPI flow verifier are in this tree. No
production-signed catalog or private signing material is present in the
repository.

The Debug workflow is for a controlled, backed-up Windows development system in
`TESTSIGNING` mode. It does not make the driver publicly distributable. See
[VIRTUAL_AUDIO_DRIVER.md](VIRTUAL_AUDIO_DRIVER.md) for the exact local
validation and production-signing boundary.

## Privacy and security boundary

The paired capture endpoint is an ordinary Windows microphone endpoint. Any
process that Windows permits to use microphones can select it and receive the
audio currently rendered to `Cuelet Virtual Microphone Input`. Cuelet does not
provide a second authorization layer, encryption boundary, or per-consuming-app
access list.

The kernel driver does not open or record a physical microphone by itself.
Physical microphone mixing occurs only in Cuelet's user-mode AudioGraph after
the user selects an input and Windows grants microphone permission. Closing
Cuelet normally stops that graph and its render streams; the virtual capture
endpoint then produces no Cuelet mix.

Public distribution still requires:

- an explicit in-app indication when physical-microphone mixing is active;
- documentation telling users to select `Cuelet Virtual Microphone`, not a
  physical microphone, in the consuming application;
- a least-privilege review of the inherited SysVAD device security descriptor;
- removal of unused sample endpoints, transports, APO/keyword sections, and
  other unreachable package surface;
- verification that install, upgrade, recovery, and uninstall never modify
  unrelated audio devices.
