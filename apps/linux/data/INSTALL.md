# Installing Cuelet for Linux

This archive contains a conventional `/usr` installation tree. Install it
system-wide from the archive root with:

```bash
sudo cp -a --no-preserve=ownership usr/. /usr/
sudo gtk4-update-icon-cache -f -t /usr/share/icons/hicolor
sudo update-desktop-database /usr/share/applications
```

Then launch Cuelet from the application menu or run `/usr/bin/cuelet`.

For an unprivileged install, copy the contents below `usr/` into `~/.local/`
instead and refresh the equivalent caches under `~/.local/share`. The executable
will be `~/.local/bin/cuelet`.

The archive uses normal system libraries rather than bundling them. Cuelet
requires GTK 4.10 or newer, libadwaita 1.5 or newer, GLib/GIO, GStreamer 1.20
or newer with playback codecs, GStreamer PBUtils, and JSON-GLib. The optional
virtual microphone additionally needs a running PipeWire session, WirePlumber
or another session manager, `pw-loopback`, and GStreamer's `pipewiresink`.
Desktop-wide shortcuts need XDG Desktop Portal and a desktop backend that
implements `org.freedesktop.portal.GlobalShortcuts`.

To uninstall a manual system-wide installation, remove the paths listed in
`usr/share/doc/cuelet/installed-files.txt`, then refresh the icon and desktop
caches. Package-manager installations should use the package manager instead.
