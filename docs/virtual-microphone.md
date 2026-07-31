# Virtual Microphone Notes

Cuelet's native Linux client can expose an app-managed PipeWire virtual
microphone. The legacy root Qt client and the other native clients do not gain
that behavior from the Linux implementation.

Virtual microphone behavior remains platform-specific. Linux creates only
temporary user-session nodes while Cuelet is running; it does not install a
kernel driver, root service, or permanent PipeWire/WirePlumber configuration.
Support must not be inferred for another platform or packaging sandbox.

## Current Boundary

On Linux, GStreamer sends Cuelet audio to an exact app-owned PipeWire sink and
managed `pw-loopback` helpers expose the corresponding source and optionally
mix one explicitly selected physical microphone. Speakers-only,
virtual-microphone-only, and simultaneous speaker/virtual playback are separate
application modes. See `apps/linux/README.md` and
`docs/cross-platform-catch-up/LINUX_VALIDATION.md` for requirements and tested
scope.

## Platform Direction

- Linux: implemented with PipeWire on the native GTK client; no PulseAudio
  compatibility module is required for the graph.
- macOS: document BlackHole or similar virtual devices, then route Cuelet output to the chosen device.
- Windows: document VB-Cable or similar virtual devices, then route Cuelet output to the selected device.

## Future UI

The native Linux preferences already separate normal playback, virtual
soundboard injection, and optional physical-microphone mix levels. Equivalent
behavior on other platforms remains future work.
