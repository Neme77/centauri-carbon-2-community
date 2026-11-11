# Upgrade to Community Firmware 4.4.4 — ELEGOO base 02.01.01.13

**Read this entire guide before starting. The preparation steps are mandatory
for an existing 4.4.3 Community installation. Flashing the firmware first is
not a substitute for preparation.** This is a complete firmware release with
CC2 Control 1.2.29, based on ELEGOO 02.01.01.13; it is not a panel-only update.

**First-ever Community installation from original ELEGOO firmware: skip the
probe and Prepare-Upgrade. They prepare existing Community installations, not
a printer that has never had Community Firmware. Follow the original-firmware
offline installation route below.** A stock reflash on a previously modified
printer is not a first installation and may leave persistent Community files.

## Choose the correct route

| Starting point | Required route |
| --- | --- |
| Community 4.4.3, any panel version, no Canvas or RFID never enabled | Back up, run the preparation plan, apply it successfully, then install the firmware. The tool checks the native baseline and supported component evidence, not the panel version. |
| Community 4.4.3 with modified Canvas firmware | Restore the original Canvas firmware and complete its startup restoration first. Then back up, prepare and install. |
| Unfinished RFID activation/restoration, missing or inconsistent records | Stop and finish recovery. Do not delete markers or journals to force an upgrade. |
| Original ELEGOO .13 with no previous Community installation | **Do not run the probe or Prepare-Upgrade.** Install the signed complete firmware using Offline Update on the original touchscreen. A stock reflash on a previously modified printer does not prove that its persistent Community files are gone. |
| A private .13 preview or a different/unknown Community base | Request a reviewed migration route. Do not run the 4.4.3 preparation script on it. The exact RC2 active-RFID to RC4 route was separately tested; that is not approval of all previews. |
| The accepted 4.4.4 RC4 build | The final RC5 changes only five translation files; backend and native components are identical. Use a reviewed .13 migration route if installing it. Do not run the 4.4.3 preparation tool on RC4. |

You do not need to install CC2 Control 1.2.27 or 1.2.28 first. A supported
4.4.3 printer can prepare directly. Compatibility is established by the tool's
checks, not by the panel version alone.

## 1. Meet the requirements

Keep the printer idle and cool, with heater targets at zero. Finish printing,
calibration, drying, scans and any pending update/recovery. Keep power and the
network connection available throughout preparation. Use a Windows PC with
PowerShell 5.1 or later, OpenSSH `ssh`/`scp`, and `tar` available. Docker and WSL
are not required to use the distributed preparation tools.

If Canvas RFID was enabled, use **Settings → Integrations** to restore the
original Canvas firmware. Follow every restart and **continue validation**
prompt. Do not continue while a reboot or recovery remains pending. Where the
lifecycle API exists, this read-only SSH command shows the state:

```sh
wget -qO- http://127.0.0.1:8081/api/rfid/lifecycle; echo
```

A completed restoration reports valid metadata, phase 10, `busy:false`, and no
pending reboot or recovery. A missing endpoint on an older panel is not proof
that RFID is absent: the preparation inspector reads the component records
directly. It refuses unsupported or incomplete evidence.

## 2. Save backups on the PC

Export the CC2 Control backup from the panel. Record the LAN access code,
connection settings, filament assignments and other settings you need.
In the extracted upgrade-kit folder, open PowerShell and run:

```powershell
.\Backup-Community-State.cmd
```

Wait for `BACKUP VERIFIED`. This archive contains the persistent `cc2-*` and
`.cc2-*` Community paths. It is **not a complete disk backup** and does not
independently include every Wi-Fi file, G-code file or native configuration.
Keep your panel export and any separate backups of those data too. Keep the
archive private: it may contain credentials and personal configuration.

## 3. Review the preparation plan

```powershell
.\Prepare-Upgrade.cmd
```

Enter the printer's current IPv4 address and existing SSH credentials. This
copies a small read-only inspector to temporary storage and lists the proposed
quarantine contents. Persistent configuration is unchanged. Review the list.
The tool recognises absent, dormant or coherently restored protocol-1 Canvas
state. It never executes the retained old Canvas programs.

## 4. Apply preparation before flashing

```powershell
.\Prepare-Upgrade.cmd -Apply
```

The tool queries the native console for idle/cold conditions; it does not wait
for MQTT or UDS freshness. It checks the supported native baseline, snapshots
and restoration records, locks, active processes and mounted paths. It copies
the selected paths to the PC, verifies the archive and checks that the source
files have not changed before moving them into a private printer quarantine.

**Continue only after `QUARANTINE COMPLETE`.** Keep the displayed PC backup
folder and printer quarantine path. If any step reports `STOP`, an error or an
incomplete transfer, keep all files and obtain help. Do not flash, delete the
RFID directory, remove the installation lock or manually bypass the check.

The allowlist covers recognised old installer stages/backups, the verified
inactive RFID component and an eligible empty Reactor-disable marker. It does
not wipe the printer. Personal data and confirmed network preferences are not
quarantined. Old Connections components are retained separately by the new
firmware's verified first-boot migration. An overlay/slot transition can still
affect settings on an unusual installation, so backups remain necessary.

## 5. Install the complete firmware

Verify the downloaded archive against `SHA256SUMS.txt`. Extract the upgrade
kit on the PC. Copy **`CC2_4.4.4_02.01.01.13.zip.sig` unchanged** to a USB drive;
do not unzip the signed firmware file or rename its internal members. Install
it through the printer's original offline firmware-update screen. Keep power
connected and let the printer complete its restart.

This file uses the stock-compatible signing route tested on the printer. The
outer release filename changes; the verified RC5 firmware bytes do not.
Compared with the physically accepted RC4, only five translation files change.

## 6. Verify the first boot

Check that the touchscreen becomes usable, temperatures are populated and
Canvas is recognised if connected. Open CC2 Control and verify version 1.2.29,
saved settings, plates/meshes and LAN access. Test the camera and your normal
network route. Keep the backup and quarantine until these checks are complete.

Canvas RFID activation remains an explicit choice under **Settings →
Integrations**. A non-Combo printer needs no RFID activation. If you enable it,
follow the complete activation, restart and validation sequence before scanning.
The decoded spool data must be reviewed before assigning a slot. Other tag
brands/layouts need testing; open and extensible does not mean every tag is
already supported.

If the printer shows zero temperatures or Canvas continues flashing red, do
not start a print or flash Canvas again. Keep power/network available and send
the preparation result and boot diagnostics to the maintainers. Do not restore
old startup programs wholesale from the archive onto the new firmware.

## Recovery and support

The preparation procedure creates and verifies a PC backup before quarantining
existing Community components. Retain that archive, the quarantine and all logs
until the installation has been verified. Recovery and return-to-original
firmware have been tested in the supported conditions described in this guide.
This is not an unconditional guarantee that every error on every installation
is reversible without loss or damage.

If preparation fails, do not flash until the failure has been reviewed. If
first-boot checks fail, do not start a print. [Open a support
issue](https://github.com/Neme77/centauri-carbon-2-community/issues/new/choose)
with your starting firmware and panel versions, the exact package filename,
the failed step and its log. Remove passwords, LAN access codes and tokens.
Keep backup and quarantine contents unchanged, and do not delete lifecycle
records or copy old startup files over the new firmware. Maintainers can help
select the appropriate recovery route. Returning to original ELEGOO firmware
requires completing the Canvas restoration procedure first when modified
Canvas firmware is active.

## Future updates

Panel feature updates stay on the normal application-update path when their
declared base is compatible. A firmware/native change with uncertain
compatibility requires a new complete firmware base and a documented migration.
The intent is to avoid repeating this historical cleanup, while keeping
compatibility checks explicit rather than promising every future update is safe.
