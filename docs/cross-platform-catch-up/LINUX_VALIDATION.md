# Linux validation record

This is the authoritative Linux 0.1.0 release record. It supersedes the
earlier ARM64/container result and records validation of the exact package
inputs on native Ubuntu/GNOME/Wayland x86_64. Generated builds, archives,
recordings, logs, and temporary directories were not retained in the
repository.

## Exact candidate and environment

| Item | Observed value |
|---|---|
| Audited GitHub base | `9d552362885d71f982787a8d4b1f0c0351c7f9d5` |
| Validated package-input commit | `261a0273b877e9a3f4804ac22c7eac7658fa8fe2` |
| Branch | `feat/linux-parity-catch-up` |
| Working tree before correction | clean; local HEAD equaled `github/feat/linux-parity-catch-up` |
| Derived `SOURCE_DATE_EPOCH` | `1787332835` |
| Distribution | Ubuntu 26.04 LTS |
| Kernel | Linux 7.0.0-30-generic x86_64 |
| Architecture | `x86_64` |
| Desktop/session | GNOME / Wayland (`ubuntu:GNOME`) |
| GTK | 4.22.4 |
| libadwaita | 1.9.1 |
| GStreamer | 1.28.2 |
| PipeWire | 1.6.2 |
| WirePlumber | 0.5.13 |
| Meson / Ninja | 1.10.1 / 1.13.2 |
| Compiler | GCC/G++ 15.2.0 |
| JSON-GLib | 1.10.8 |

The host is the primary supported validation scope: Ubuntu 26.04 LTS,
GNOME, Wayland, and x86_64. No claim is made for other distributions, KDE,
Flatpak, Snap, or receiving applications beyond the clients named below.

## 2026-08-21 stable-input Release validation

The committed `apps/linux/scripts/package-linux-release.sh` pipeline was run
twice from clean temporary build/staging directories with
`SOURCE_DATE_EPOCH` unset. The script derived epoch `1787332835`, the newest
Git commit timestamp across the tracked inputs that can affect the installed
Linux payload or deterministic archive construction. Unpackaged documentation
and tests do not influence that default. Each run performed an optimized Meson
Release build with `-Dwerror=true`, `-Dstrip=true`, and the supported Ninja
workflow.

Both runs passed:

- build and warnings-as-errors compilation;
- all 15 registered Meson tests;
- the focused About-dialog test;
- release metadata tests;
- desktop-file validation;
- AppStream pedantic validation;
- XML, padded-icon, and version checks;
- PipeWire routing and routing-service tests;
- virtual-microphone tests;
- GStreamer audio-service tests;
- stripped Release payload and unresolved-dependency checks;
- no-developer-path and archive-content checks; and
- release hygiene.

The test suite result was 15 passed, 0 failed on each run. The release
identity checks confirmed version `0.1.0`, executable `cuelet`, application ID
`io.cuelet.Cuelet`, and the final padded SVG icon. Release builds keep
`developer_tools` disabled; no demo-library development behavior is included.
The Linux source has no Qt dependency or legacy root CMake build path.

## Release archive

| Item | Result |
|---|---|
| Filename | `Cuelet-0.1.0-linux-x86_64.tar.gz` |
| Size | 568,213 bytes |
| SHA-256 | `4d9d5eb5e9ab4fa6db9bdb66bc2c0c585d992c4a13d8e2346ca198b4c4a5cfa4` |
| Reproducibility | Two equivalent clean builds were byte-identical |

A separate disposable clone then received a later documentation-only commit
outside the Linux package-input set. With `SOURCE_DATE_EPOCH` still unset, the
resolver retained epoch `1787332835`; a third complete package build produced
the same 568,213-byte archive byte-for-byte. The disposable commit, clone,
build, and archive were removed after comparison and never entered the release
branch.

The inspected archive contained the `cuelet` executable, desktop file, padded
scalable SVG icon, AppStream metadata, packaging README and installed-file
manifest, and the full AGPL `LICENSE`. The executable was stripped and had no
debug/symbol sections or unresolved shared-library dependencies. No source,
debug, test, build-directory, recording, or developer-path residue was found.
The generated archive, build directories, and staging directories were
deleted after inspection and are not tracked.

## Live GNOME/Wayland and PipeWire evidence

The durable runtime records from the 2026-07-31 native GNOME/Wayland session
remain valid for the implemented Linux behavior. They cover the real GTK
application, isolated settings, library/import/playback/lifecycle flows,
portal shortcuts, and the app-owned temporary PipeWire graph. The virtual
microphone flow used `pw-record` as the receiving client and measured Cuelet
injection, optional selected-source mixing, combined speaker/virtual routing,
cleanup, and helper recovery. It did not claim Discord, OBS, Firefox, games,
KDE, Flatpak, or Snap compatibility.

The live record also confirms that Cuelet does not change the default PipeWire
devices, write persistent PipeWire/WirePlumber configuration, record the
physical microphone to disk, or transmit audio over the network. A daemon
restart and an application-specific receiving client with an explicit input
selector were not tested because they would disrupt the session or were not
available; those remain explicit limitations.

## Manual Linux desktop scope

The durable GNOME/Wayland runtime evidence covers real GTK launch,
populated/empty library, categories, search, playback, settings, responsive
layout, and routing states. File-dialog import, drag-out, broad accessibility
audits, and third-party receiving applications remain untested limitations.
This validation cycle did not expand into broad application or desktop-matrix
testing. Superseded capture-session checklists and image inventories are not
part of the authoritative release record.

## Release conclusion

Linux source, functional, and legal/package checks passed for the stated
Ubuntu/GNOME/Wayland x86_64 scope. Linux has no kernel-driver signing step.
Broader distribution packaging, receiving-client compatibility, and complete
desktop accessibility coverage remain follow-up work, not release claims.
