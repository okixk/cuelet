# Cuelet 0.1.0 cross-platform release evidence

This directory contains the authoritative platform validation and capability
records for the Cuelet 0.1.0 release candidate. Runtime claims come only from
the named platform records; source presence is not promoted to runtime proof.
Superseded planning snapshots, capture-session tooling, and historical image
inventories are intentionally excluded from the release branch and remain
available through Git history.

Start with:

- [cross-platform validation](CROSS_PLATFORM_VALIDATION.md) for the release
  summary and publication boundary;
- [feature parity matrix](FEATURE_PARITY_MATRIX.md) for evidence-qualified
  capability comparisons;
- [macOS validation](MACOS_VALIDATION.md), [Windows validation](WINDOWS_VALIDATION.md),
  and [Linux validation](LINUX_VALIDATION.md) for platform-specific results;
  and
- [macOS virtual-audio driver contract](MACOS_VIRTUAL_AUDIO_DRIVER.md) for the
  enduring HAL architecture, privacy, and real-time constraints.

The six approved public screenshots are maintained under `docs/images/` and
are presented by the repository root `README.md`; they are separate from these
validation records.
