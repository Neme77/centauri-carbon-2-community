# Extended Canvas pull-back — R1 pilot

Standalone experimental installer for **Community Firmware V4.4.2 / CC2 Control 1.2.21**. Reproduces **Noahwhite2014**'s #172 / #202 change: raises the accepted maximum from 350 to 1000 mm. It does not set the distance, eject filament, request movement, restart services or reboot. It does not change the webcam or CC2 Control.

**Controlled first trial: this installer has not yet been validated on a printer.** Fingerprint checks do not establish a suitable physical distance. The contributor reported 560 mm; unloading/reloading at 1000 mm remains unqualified.

## Requirements and check

Python 3.10+ and OpenSSH client on Windows, Linux or macOS. SSH credentials are entered in the terminal and are not saved by this package. Normal SSH host-key checking remains enabled.

The printer must be connected, Idle, without a paused print, both heaters at most 50 °C with targets zero; at least 128 MiB persistent free space is required after staging. Only the exact pinned program, launchers and runtime are accepted. Included/ambiguous configuration is refused.

Extract the complete ZIP. Windows: open `Start-Windows.cmd`. Linux/macOS: run `sh Start-Linux-macOS.sh`. Select **1 — Check**, enter the printer IP and share the result with the maintainer before installation.

After arranging the pilot, select **3 — Install**, type `INSTALL PULLBACK`, and keep the terminal open. Only after `INSTALLATION PASSED`, reboot the idle printer normally. Select **2 — Status** and verify the extended program, Canvas, local/Matrix video and normal loading. Installation leaves the configured distance unchanged; arrange the physical test separately.

CLI: `python manage.py check --host IP` (`python3` on Linux/macOS). Actions: `check`, `status`, `install`, `remove`.

## Removal and updates

**Remove this component and reboot before any app/firmware update.** Ordinary installers require the original program fingerprint and refuse the extended one. Changing update channels does not remove this component.

Choose **4 — Remove**, confirm `REMOVE PULLBACK`, wait for `REMOVAL PASSED`, then reboot while Idle. Removal first clamps only `unload_fila_dist` values above 350 to 350 in `printer_dsp.cfg`, `autosave.cfg` and optional `autosave_backup.cfg`; it then restores the original executable and launchers. Other settings/meshes remain unchanged. Backups remain under `/opt/usr/cc2-canvas-pullback-1000.removed-*`.

Do not change distance between removal and reboot: the extended process is still running until it exits. Check **Status** after reboot before updating.

If interrupted, retain the output and inspect **Status** before rebooting. Removal supports partial transactions only when every current file is a known original/patched version and all original backups remain valid. Foreign changes are refused.

## Contents and attribution

Sources/scripts only: no vendor executable, credentials or signing material. Transformation takes place on the PC using a copy read from the printer; input and output SHA-256 are pinned. Original patch/idea: **Noahwhite2014**. Installation/removal packaging: CC2 Community project. This is separate from the abandoned complete filament-ejection project.
