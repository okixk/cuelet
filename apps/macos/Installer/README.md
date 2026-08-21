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
change audio defaults. The product does not request an Installer restart
conclusion: installation ends with Close, and the user restarts macOS later at
a convenient time to activate the driver.

Build a structural local package with no private credentials:

```bash
cd apps/macos
./scripts/build-release-package.sh --local
```

The result is named `Cuelet-<version>-local.pkg` and is explicitly a local test
artifact, not a public distribution package. Its Welcome, Read Me, and Summary
pages all display `LOCAL TEST PACKAGE — NOT FOR PUBLIC DISTRIBUTION`.

The public beta uses a separate, explicit unsigned mode:

```bash
./scripts/build-release-package.sh --beta-unsigned
```

It emits `Cuelet-0.1.0-beta.1-macos-arm64-unsigned.pkg`, keeps Cuelet at
version 0.1.0 build 1, and displays a concise beta/security-warning notice on
each Installer page. The beta package has no code signature, notarization, or
private signing data. It includes both `License.txt` (Cuelet's AGPL license)
and `APPLE_SAMPLE_LICENSE.txt` (Apple's separate sample-code notice) in its
component payloads; the notice is also carried in the installed app and
driver bundles.

The product archive uses polished Cuelet Welcome, Read Me, and Summary copy.
Finder and the Installer title bar represent the outer `.pkg` with macOS's
standard Installer package icon. The application payload retains and validates
the final compiled Cuelet icon; no duplicate Installer-only icon is maintained.

For a future public build, configure identities outside the repository:

```bash
export CUELET_DEVELOPER_ID_APPLICATION='Developer ID Application: ...'
export CUELET_DEVELOPER_ID_INSTALLER='Developer ID Installer: ...'
./scripts/build-release-package.sh --release
```

`CUELET_SIGNING_KEYCHAIN` may name a non-default keychain. Release mode never
falls back to ad-hoc or unsigned output. It signs the nested driver, signs the
application, and signs the final product archive. Component packages remain
unsigned because the signed product archive protects them. Its default filename
is `Cuelet-<version>.pkg`; its rendered Installer pages never contain the local
test warning. Without both configured identities, release mode exits before
building any public-looking artifact.

Notarization is intentionally a separate release operation. A future release
job should submit the signed product archive with `notarytool`, staple the
accepted ticket to that same package, and verify it with `stapler`, `pkgutil`,
and Gatekeeper. Credentials or notary profiles must not be stored in Git.

Reinstalling the package repairs a valid same-version Cuelet installation.
Uninstall remains a separate release follow-up: a signed removal package or
other intentional administrator-approved tool is preferable to adding a
persistent privileged helper.

## Installation domains and app-only use

The full product enables only the local-system installation domain. Both
component packages use fixed root-relative destinations, and the HAL driver
must remain at `/Library/Audio/Plug-Ins/HAL`; it cannot be installed into a
user home. Consequently Installer correctly offers only “Install for all users
of this computer,” requires administrator authorization, and leaves “Install
for me only” disabled.

The Cuelet application itself is self-contained for sound-library management,
normal playback, and output routing. It can run from a user-owned location such
as `~/Applications`; without the full Installer, the app reports the bundled
driver as prepared/not installed and does not claim the virtual microphone is
ready. For 0.1.0 the existing Release `.app` is the app-only local-test path.
A future app-only download should use an ordinary drag-and-drop DMG rather than
a second component package. No app-only public artifact is emitted today.

The two component packages remain one non-customizable product. Exposing the
driver as an optional Customize choice would still leave the product in the
system domain and require administrator authorization, so it would not solve
the per-user case and would add an ambiguous installation path.

Installer owns payload transaction and rollback behavior. Identity/version
guards run before replacement; a failed guard leaves both destinations
unchanged. A driver post-install verification failure is returned to Installer
as a package failure instead of being hidden. Read-only/full volumes and
authorization failures are handled by Installer, with no custom backup tree or
partial-copy fallback. If Cuelet is open during an update, the package replaces
the on-disk app atomically; the running process remains independent of the
not-yet-loaded driver and the next normal app launch uses the replaced bundle.
The Summary states that Cuelet is ready immediately and that a later restart is
required only to activate Cuelet Virtual Microphone.
