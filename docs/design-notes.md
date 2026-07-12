# Design Notes

## Current Decision

Cuelet will use native frontends per platform instead of a single custom cross-platform UI. The macOS app starts with SwiftUI and can use AppKit only where SwiftUI is not enough.

## Why

The Qt prototype proved the product shape, but native macOS details such as toolbar menus, pop-up buttons, chevrons, focus behavior, settings windows, and titlebar behavior are hard to fake convincingly.

## First macOS Foundation

The first native pass prioritizes:

1. Project and source structure.
2. Native window, sidebar, and toolbar.
3. Sound Library screen.
4. Polished large sound pads.
5. Clean settings view.
6. Basic service/model layer.
7. Notes for future shared backend.
