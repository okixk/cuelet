# Cuelet Core

This folder is reserved for shared backend logic that can support multiple native frontends.

Keep platform UI code out of this layer. Candidate responsibilities include library scanning, supported audio file detection, metadata, categories, favorites, recent sounds, profiles, search/filter logic, import/export format, settings models, and hotkey definitions.

The first native macOS version keeps services in Swift under `apps/macos/Cuelet/Services` until the boundaries are clearer.
