# cuelet-core

`cuelet-core` contains toolkit-neutral C++17 data and library behavior used by the native Linux frontend.

Current responsibilities:

- Sound, category, shortcut, and metadata data models using standard C++ types.
- Supported audio file detection.
- Library scanning with optional recursion.
- Stable sound/category identifiers.
- Search, filtering, and sorting.
- In-library `.cuelet-metadata.json` load/save through a small API.

The Linux Meson build links these sources directly. Windows compiles the
toolkit-neutral subset into its native core library.

The public API is C++ today. macOS can continue using Swift services until there is a clear reason to add a Swift bridge or another FFI boundary.
