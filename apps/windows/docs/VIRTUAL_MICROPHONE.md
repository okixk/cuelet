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

Cuelet 0.1.0 uses a separately installed VB-CABLE device for Windows Release
voice-chat routing. Cuelet links to `https://vb-audio.com/Cable/`, but does not
download, redistribute, install, repair, or uninstall the third-party driver.
Follow VB-Audio's administrator installation process and restart Windows, then
refresh Cuelet's audio devices or run Audio Setup again.

Cuelet recognizes and pairs:

- broadcast render: `CABLE Input (VB-Audio Virtual Cable)`
- recording endpoint shown to other apps: `CABLE Output (VB-Audio Virtual Cable)`
- physical input: the selected or default communications microphone
- physical microphone mixing: optional

Local playback and monitoring are preserved through a separately selected
speaker/headphone endpoint. Cuelet does not change the global Windows default
microphone. Select `CABLE Output` as the microphone in Discord, games, OBS, or
the receiving application.

## Current delivery boundary

Release packaging excludes Cuelet's development driver package and elevated
installer helper. VB-CABLE support uses endpoint discovery and ordinary Windows
audio APIs, so no VB-Audio binary or signing material is present in the
repository or MSIX.

Driver source preparation, the render-to-capture bridge, test-signed Debug
packaging, the elevated helper, and an independent WASAPI flow verifier remain
in this tree strictly for Cuelet driver engineering. No production-signed
Cuelet driver catalog or private signing material is present in the repository.

The Debug workflow is for a controlled, backed-up Windows development system in
`TESTSIGNING` mode. It does not make the driver publicly distributable. See
[VIRTUAL_AUDIO_DRIVER.md](VIRTUAL_AUDIO_DRIVER.md) for the exact local
validation and production-signing boundary.

## Privacy and security boundary

The VB-CABLE capture endpoint is an ordinary Windows microphone endpoint. Any
process that Windows permits to use microphones can select it and receive the
audio currently rendered to `CABLE Input`. Cuelet does not
provide a second authorization layer, encryption boundary, or per-consuming-app
access list.

VB-CABLE does not cause Cuelet to open a physical microphone by itself.
Physical microphone mixing occurs only in Cuelet's user-mode AudioGraph after
the user selects an input and Windows grants microphone permission. Closing
Cuelet normally stops that graph and its render streams; the virtual capture
endpoint then produces no Cuelet mix.

Public distribution documentation must keep the third-party dependency and
license clear, tell users to select `CABLE Output` rather than their physical
microphone in the consuming application, and indicate when physical-microphone
mixing is active.
