# CC2 Control 1.2.26 — application and native components

This cumulative update is for the exact supported **Community Firmware V4.4.3**
profile on the Centauri Carbon 2 (ELEGOO base 02.01.01.07, Canvas 00.00.00.44).
It does not replace the native touchscreen or the base firmware. Stock, V4.3,
modified startup files, unfinished native operations and unknown profiles are
refused rather than overwritten. Update an older Community Firmware base to
V4.4.3 before using this complete package.

One installer selects first installation, supported active RFID update or an
already current installation automatically. Computer and signed panel updates
use the same payload. Native/runtime hashes and the live printer state are
verified independently of the displayed version.

## Install

Use an Idle, cold printer with both heater targets zero, no printing, scans or
calibrations, and close camera viewers. Keep power and the local connection
available. Windows: extract the whole ZIP and run **Install-Windows.cmd**.
Linux/macOS: `sh ./install-cc2-control.sh PRINTER-IP`. OpenSSH is required; use
root and the SSH password, separately from the panel LAN access code. Existing
users can instead install the signed update through Settings → Updates.

The installer retains configuration, materials, preferences, plates, spools and
calibrations. Record its printed backup path. Payloads use an exclusive
persistent `/opt/usr` stage, not RAM-backed `/tmp`; at least 64 MiB free disk and
24 MiB available RAM are required before transfer. Hashes, health, MQTT and
running-app identity are checked. On success payload files are cleaned; logs
and rollback backups remain. On failure preserve the reported stage and log.

## First RFID activation

The package includes the EXHAUST minimum component, camera/recovery fixes and
Connections runtime as well as the guarded RFID activation flow. It first saves
and verifies the original Canvas application already present on the printer,
then stages the host protection. **Follow the full idle-printer reboot prompts**
in Canvas → RFID. After the guard is verified on the next boot, activation
modifies the Canvas application to remove its producer filter. A further full
reboot verifies the Canvas and native guard. Canvas LEDs can flash red while
the native process is stopped for transfer. Do not interrupt power or reboot
during a transfer. No automatic material assignment, heating or motion occurs.

An already active supported update preserves Canvas, loaded native libraries,
startup files and the original backup; only the application and four linked
RFID files are rebound. It performs no unnecessary Canvas firmware transfer.
An already current installation is verified without replaying activation.

RFID scans wait for fresh native/MQTT observations before arming. Present the
tag only after the scan is armed, review the preview, choose a Canvas slot and
confirm. Assignment and physical filament presence are separate: insert the
filament in the chosen inlet to mark it present. An Anycubic PETG tag and a
Creality spool were physically read; this does not establish all-brand/tag
compatibility. The transfer protocol confirms ACKs, not physical flash readback.

## Recovery and restore

Keep the exact restore command printed by your installation. For a supported
app-only upgrade it restores the prior app and its linked RFID files together.
Before removing the native host protection or changing base firmware, use the
RFID panel flow to restore the original Canvas, complete its required full
reboot and verification, then remove the host protection and complete its
reboot. Do not remove the guard while the modified Canvas remains active.

If recovery is required, keep power connected, retain journal/logs/backups and
follow the reported recovery state. Do not delete locks or overwrite startup
files. A completed software rollback does not claim a physical flash readback.

## SSH host-key changes

Only for an expected change use `-ResetHostKey` (Windows) or `--reset-host-key`
(Linux/macOS). Confirm the exact printer address; verify the new SSH fingerprint.
Only that address is removed from default known_hosts. Custom known_hosts or
HostKeyAlias require manual handling.

[Release notes](https://github.com/Neme77/centauri-carbon-2-community/releases/tag/CC2-Control-1.2.26-V4.4)
· [English guide](https://github.com/Neme77/centauri-carbon-2-community/blob/develop/docs/UPDATE_CC2.md)
· [Italian guide](https://github.com/Neme77/centauri-carbon-2-community/blob/develop/docs/UPDATE_CC2.it.md)

The build/signing gate accepts only the reviewed app/native artifact hashes in
`installer/native-release.json`. The package contains no vendor firmware image,
private signing key or printer credentials. Source and native runtime provenance
are attached to the release; exact runtime/profile admission remains required.
