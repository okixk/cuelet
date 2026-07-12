# App Icon Export Notes

The current Qt prototype icon is intentionally not redesigned here. When replacing the placeholder, export a macOS app icon with these principles:

- Keep the foreground mark inside roughly 76-82% of the canvas.
- Preserve visible internal padding at 16, 32, and 64 px rendered sizes.
- Avoid touching the rounded-square bounds.
- Export the full macOS app icon set: 16, 32, 128, 256, 512, and 1024 px PNGs.
- Reuse the same balanced source artwork for Windows `.ico` and Linux PNG exports.

