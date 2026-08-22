# Virtual Microphone

Virtual microphone behavior is platform-specific:

- macOS: the beta package installs Cuelet's Core Audio HAL device. A Mac
  restart is required before the device becomes available.
- Linux: Cuelet creates temporary user-session PipeWire nodes while it is
  running. It does not install a kernel driver, root service, or persistent
  PipeWire/WirePlumber configuration.
- Windows: the 0.1.0 Release app uses a separately installed VB-CABLE pair.
  Cuelet's development virtual-audio driver is not included in the beta.

## Current Boundary

On Linux, GStreamer sends Cuelet audio to an exact app-owned PipeWire sink and
managed `pw-loopback` helpers expose the corresponding source and optionally
mix one explicitly selected physical microphone. Speakers-only,
virtual-microphone-only, and simultaneous speaker/virtual playback are separate
application modes. See `apps/linux/README.md` and
`docs/cross-platform-catch-up/LINUX_VALIDATION.md` for requirements and tested
scope.

The native Linux preferences separate normal playback, virtual soundboard
injection, and optional physical-microphone mix levels. See
`apps/linux/README.md` and `docs/cross-platform-catch-up/LINUX_VALIDATION.md`
for the Linux routing requirements and tested scope.
