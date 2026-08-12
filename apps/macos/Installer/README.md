# Cuelet macOS Installer

The public macOS artifact is a product archive with two component packages:

| Component | Package identifier | Destination |
| --- | --- | --- |
| Cuelet application | `ch.oki.cuelet.pkg.application` | `/Applications/Cuelet.app` |
| Cuelet audio driver | `ch.oki.cuelet.pkg.virtual-audio` | `/Library/Audio/Plug-Ins/HAL/CueletVirtualAudio.driver` |

The product identifier is `ch.oki.cuelet.installer`. Both bundles use atomic
bundle replacement, strict bundle identifiers, fixed destinations, and version
checks. Package scripts refuse to replace a foreign bundle at either exact
destination and refuse to downgrade a newer Cuelet bundle. The driver
post-install check validates its identity, version, architecture, and code
signature, then updates the `Info.plist` modification time used by Cuelet to
show a reliable restart-required state. It does not restart Core Audio or
change audio defaults.

Build a structural local package with no private credentials:

```bash
cd apps/macos
./scripts/build-release-package.sh --local
```

The result is named `Cuelet-<version>-local.pkg` and is explicitly a local test
artifact, not a public distribution package.

The product archive's welcome screen embeds a 256 px rendition derived from
the `Cuelet.icns` inside the staged application. This keeps Installer branding
synchronized with the app icon without checking in another raster export.
Finder continues to represent the outer `.pkg` with macOS's standard Installer
package icon; product archives do not provide a supported custom Finder icon.

For a future public build, configure identities outside the repository:

```bash
export CUELET_DEVELOPER_ID_APPLICATION='Developer ID Application: ...'
export CUELET_DEVELOPER_ID_INSTALLER='Developer ID Installer: ...'
./scripts/build-release-package.sh --release
```

`CUELET_SIGNING_KEYCHAIN` may name a non-default keychain. Release mode never
falls back to ad-hoc or unsigned output. It signs the nested driver, signs the
application, and signs the final product archive. Component packages remain
unsigned because the signed product archive protects them.

Notarization is intentionally a separate release operation. A future release
job should submit the signed product archive with `notarytool`, staple the
accepted ticket to that same package, and verify it with `stapler`, `pkgutil`,
and Gatekeeper. Credentials or notary profiles must not be stored in Git.

Reinstalling the package repairs a valid same-version Cuelet installation.
Uninstall remains a separate release follow-up: a signed removal package or
other intentional administrator-approved tool is preferable to adding a
persistent privileged helper.

Installer owns payload transaction and rollback behavior. Identity/version
guards run before replacement; a failed guard leaves both destinations
unchanged. A driver post-install verification failure is returned to Installer
as a package failure instead of being hidden. Read-only/full volumes and
authorization failures are handled by Installer, with no custom backup tree or
partial-copy fallback. If Cuelet is open during an update, the package replaces
the on-disk app atomically and the required Mac restart ends the old process
before the new driver is used.
