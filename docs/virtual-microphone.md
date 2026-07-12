# Virtual Microphone Notes

Cuelet v1 does not expose a virtual microphone output. It only plays sounds to a selected audio output device through Qt Multimedia.

This is deliberate. Virtual microphone behavior is platform-specific and usually requires user-installed routing devices or privileged audio graph configuration. Pretending it works everywhere would create a confusing and brittle app.

## Current Boundary

The audio code is behind `AudioService`, so a routing backend can be added later without rewriting the UI or library scanner.

## Platform Direction

- Linux: investigate PipeWire first, with PulseAudio compatibility as a secondary route.
- macOS: document BlackHole or similar virtual devices, then route Cuelet output to the chosen device.
- Windows: document VB-Cable or similar virtual devices, then route Cuelet output to the selected device.

## Future UI

A future version should separate normal monitor playback from microphone/routing output so users can decide whether cues are heard locally, sent to chat, or both.
