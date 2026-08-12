# Windows application assets

These PNGs are the prepared Windows export of the final Cuelet artwork. The
MSIX resource system selects the matching `scale`, `targetsize`, and `altform`
variant for Start, Search, installed-app, taskbar, splash, Store, and tile UI.

`Cuelet.ico` is a lossless ICO container generated from the 16, 24, 32, 48,
and 256 pixel unplated PNG variants by
`..\..\scripts\generate-windows-icon.ps1`. It supplies the executable,
window, task-switcher, and notification-area icon.

The supplied gray `LockScreenLogo` and `StoreLogo.backup` placeholders are
intentionally excluded. Cuelet does not declare a lock-screen capability.
