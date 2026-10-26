# CC2 Control application updates

This updates CC2 Control only. It does not modify printer firmware,
elegoo_printer, the reactor/callback library or the original display updater.
Only Community Firmware V4.4 on the qualified ELEGOO 02.01.01.07 base is
supported. The independent installer rechecks the actual installed and running
printer executable fingerprints before stopping the service.

## First installation

Install the updater-enabled application package once from your computer,
using the included Windows or Linux/macOS installer. Existing LAN settings,
material profiles, spool inventory and other application data are retained.
Thereafter open **Settings → Updates**. Pause live view, wait for the printer
to be idle, cool both heaters to 50 °C or below and set both heating targets
to zero. Enter the LAN code as confirmation; it is not your SSH password.
The code is never stored in the browser's local storage or update metadata.

## GitHub distribution

GitHub Releases host `.cc2update` application assets. A small Ed25519-signed
index at `cc2-control/update-channel.cc2catalog` selects version, firmware,
channel, size and plain-text release notes. It is distinct from the repository's
latest firmware release. Only the installed public trust anchor can authenticate
an index or package. Stable is the default; the Canvas UDS experimental channel
requires an explicit warning confirmation and is independent of the stable
implementation. No experimental package is selected just because its version
number is higher.

The browser checks the index while the Updates tab is open, reusing a verified
unexpired result for up to six hours. The manual Check button refreshes it.
There is no always-running discovery service or automatic installation. An
absent index means no online update has been published yet. Normal printer
controls remain available when GitHub cannot be reached.

**Download and verify** saves the selected package to the flash filesystem
under `/opt/usr/cc2-control-updates`, using bounded buffers. It does not install
anything. A short-lived statically linked HTTPS helper verifies certificates
and hostnames and accepts only the project release URLs and GitHub's asset
redirect host. It does not depend on the printer's wget, curl, OpenSSL or unzip.
The printer clock must be correct for HTTPS certificate verification.

The package binds a canonical manifest to a streamed payload SHA-512 digest,
verified with Ed25519 before extraction. Downgrades, the same version, wrong
firmware, corruption, unsupported paths, links and duplicate archive entries
are rejected. Local signed-file uploads use the same checks. Unsigned ZIP
packages are for the first computer-assisted installation only.

**Install verified update** asks for a second confirmation. An independent
installed runner rechecks the package, launches the authenticated installer,
backs up the old installation and verifies the replacement's version,
executable and MQTT registration/snapshot. Failures after service stop trigger
rollback. Update state survives the service restart; reopen the Updates tab to
see the result. A browser close does not abort an installation that was already
accepted. Keep the printed backup and restore instructions for recovery.

## Publishing (maintainers)

Build and test the ARM package through CI, then sign its payload using
`installer/sign-update.mjs` on the maintainer's computer. Use a private key
stored outside the repository. Never copy it to the printer, source snapshots
or CI. Upload the resulting asset to its matching GitHub release. Create the
JSON index with schema 1, a future Unix `expires` timestamp and up to eight
releases. Each entry has `version`, `firmware`, `channel` (`stable` or
`canvas-uds`), `url`, `size` and `notes`. Sign it with `installer/sign-catalog.mjs`
and publish the index only after the asset is available and the printer trial
is confirmed. Renew the index before expiry; do not reuse a version number for
different package contents. Keep the old assets and their source history.

The initial updater and both channels require separate real-printer validation.
Host, signature and browser tests do not constitute printer qualification.
