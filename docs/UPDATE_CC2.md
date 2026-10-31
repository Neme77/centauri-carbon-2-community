# CC2 Control 1.2.24 update

For Community Firmware V4.4 / V4.4.1 / V4.4.2 on ELEGOO base 02.01.01.07.
Stock firmware and older bases are unsupported. The complete V4.4.2 firmware and
builder download are unchanged; this is an application update.

## Before installation

Use a connected, Idle and cold printer with heater targets zero. Close live video
viewers. Settings, meshes and spool inventory are retained; keep your existing
backups. Remove any separate extended pull-back component and reboot before
updating, following its guide. This release does not expand its qualification.

During installation and first activation of the USB Ethernet driver, Canvas may
temporarily flash red while native services realign. It becomes operational again
after realignment. Do not interrupt the installer while services realign.

## Panel and automatic update

Open Settings → Updates → Check for updates. The stable catalog offers 1.2.24.
Enter the printer's LAN access code, download and verify, then confirm installation
separately. The LAN code is different from the SSH password. Alternatively upload
CC2-Control-1.2.24-V4.4.cc2update from the release. Installer ZIPs are not panel uploads.

If the panel already reports 1.2.24, no reinstallation is needed. Reload with Ctrl+F5
after updating. A complete idle-printer reboot is required only for first activation
of a camera fix not already active; follow the installer's notice. Users already on
the active cumulative fix since 1.2.19 / complete V4.4.2 do not need an extra camera reboot.

## PC installation

For the qualified V4.4.2 runtime, download and fully extract
CC2-Control-1.2.24-Connections-Update.zip. Verify its SHA-256 against the release.
Run Install-Windows.cmd on Windows, or sh install-cc2-control.sh on Linux/macOS.
Use the printer's reachable address and SSH login. Windows needs OpenSSH Client.
The complete installer stages on /opt/usr and removes payloads only after complete
success; logs and rollback backups remain. Preserve the printed backup/restore paths.
If a same-version attempt left a staging directory, inspect and preserve its logs
before retrying; do not repeatedly reinstall over a failed attempt.

The generic CC2-Control-1.2.24-Multiplatform-Update.zip remains available for the
existing application update route. On supported printers with the updater panel,
the signed panel route avoids manual SSH staging.

## Connections

Open Settings → Connections. Enable USB Ethernet support only when wanted.
The qualified adapter is TP-Link UE300, Realtek RTL8153, USB 0bda:8153, revision
31fd, with the exact Community V4.4.2 profile. Other RTL8153 products/revisions
are not automatically qualified; RTL8156/RTL8139 are not claimed as supported.
Unsupported network profiles retain native Wi-Fi. Wi-Fi remains available without
an adapter. This release does not add automatic signal-quality selection.

Saved Wi-Fi profiles, DHCP/static IPv4, enable/disable and manual selection are
available. After switching, open the selected address and press Keep this
connection. Unconfirmed changes restore the previous connection; confirmed choices
persist across reboot. Disable the other interface from the address you intend to
keep. The last enabled interface cannot be disabled. Network actions alone omit
the LAN-code gate; updates and experimental controls retain it.

## Pending calibration edits

Restart after changing Canvas pull-back or a plate mesh before saving another
calibration. CC2 refuses conflicting calibration saves and raw console commands
while an edit is pending; emergency stop remains available. A native SAVE_CONFIG
overwrite is reported as a conflict. Newer native settings, including Z calibration,
are retained; explicitly save the desired value again before restarting.
The original display's saves remain independent. See CONFIGURATION_SAVES.md.

## Included changes and validation

Obsidian, centered sidebar layouts, direct emergency stop, long calibration bed
keep-alive, guarded busy-file deletion, localized refusals, corrected Z arrows and
pending SAVE_CONFIG protection are cumulative. Details, credits and observed
hardware-test scope are in the release notes and CHANGELOG.md. Issue #268 is not
claimed as fixed. The display-IP interception experiment is excluded.

[Release, source archives and checksums](https://github.com/Neme77/centauri-carbon-2-community/releases/tag/CC2-Control-1.2.24-V4.4).
