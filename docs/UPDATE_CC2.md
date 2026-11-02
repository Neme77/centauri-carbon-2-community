# CC2 Control 1.2.25 update

For ELEGOO Centauri Carbon 2 on compatible Community Firmware V4.4 / V4.4.1 /
V4.4.2 / V4.4.3, ELEGOO base 02.01.01.07. Exact native fingerprints are checked.
This updates the web application and its cumulative optional components, not the
native display. For the native touchscreen refresh install full firmware V4.4.3.

## Before installation

Use a connected, idle and cold printer, with both heater targets zero and nozzle
and bed at most 50 C. Close video viewers. Keep backups. Remove any separately
installed extended Canvas pull-back experiment and reboot according to its guide
before ordinary app/firmware updates; this release does not qualify that component.

During Ethernet driver installation/activation, Canvas LEDs may briefly flash red
while native services realign, then return to their normal operational state.
Wait for Canvas to reconnect and follow any complete idle-printer reboot request
before printing. Existing active camera fixes are preserved.

## Existing web panel

Open Settings -> Updates and check for updates. Download and verify 1.2.25, then
confirm installation separately using the printer's LAN access code. This differs
from the SSH password. Alternatively upload the signed
CC2-Control-1.2.25-V4.4.cc2update file. An installer ZIP is not a panel upload.

After completion, reload the page. Follow the installer result and retain the
backup/restore path. Unknown native profiles retain native Wi-Fi; they do not
force activation of an unqualified Ethernet component.

## Computer-assisted installer

Download CC2-Control-1.2.25-Multiplatform-Update.zip and extract it completely.
Check SHA256SUMS.txt from the release. On Windows run Install-Windows.cmd; on
Linux/macOS use sh install-cc2-control.sh PRINTER-IP. OpenSSH is required. Use root
and your existing SSH password. Logs and rollback backups are retained.

## Base-firmware reinstall

An exact supported base-firmware reinstall can reset hooks while retaining
/opt/usr. The installer recognizes the reviewed combination of originals,
component files and missing hooks, backs up the previous state and stages the
recovery without changing the live network. A complete idle-printer reboot is
required when requested. Foreign edits remain refused; do not remove checksums
or bypass the installer to force a recovery.

## Compatibility

The physically qualified USB Ethernet profile is TP-Link UE300 / Realtek RTL8153,
0bda:8153 revision31fd. Revision3000 is an explicit experiment; other revisions and
USB IDs are not implicitly qualified. Wi-Fi works without an adapter. Enable
Ethernet only when wanted; the stock kernel is unchanged.

## Restore

Use only the backup path and restore command printed by your own installation,
with the printer idle and cold. Persistent configuration is retained. A firmware
predecessor may launch /opt/inst/cc2-control, while an application update installs
under /opt/usr/cc2-control; this difference is expected.

## Disk staging and memory

The PC installer uses a private stage on persistent /opt/usr, not RAM-backed /tmp.
It requires at least 64 MiB free disk and 24 MiB available RAM before transfer.
The installer rechecks memory before parsing embedded components. Close viewers;
if a resource check refuses, retain the message and retry only after resolving it.
A successful attempt removes the verified input payload, retaining logs/backups.
A failed attempt is retained for diagnosis.
