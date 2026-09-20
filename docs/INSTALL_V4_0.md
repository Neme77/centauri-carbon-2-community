# Installing Centauri Carbon 2 Community Firmware V4.0

## Scope

This guide applies only to the **ELEGOO Centauri Carbon 2** and Community Firmware V4.0, built and tested against official ELEGOO firmware **02.01.00.00**.

Do not install this package on the original Centauri Carbon or any other printer model.

## Before installing

- Finish or cancel any active print and allow the heaters to cool.
- Keep the printer connected to reliable power throughout the update.
- Keep the official recovery firmware and the previous known-good community release available.
- Record the printer IP address and LAN access code.
- If you changed the SSH root password, expect to set it again after a full firmware installation.
- Verify the firmware filename and SHA-256.

PowerShell:

```powershell
Get-FileHash .\CC2_V4_0_STOCK_20260920_162601_4ab3dab2.zip.sig -Algorithm SHA256
```

Expected SHA-256:

```text
25cefa00f43ec43ced3a8b00e4240bbcaad9a8dba48c3c2154a6fa7682f8b428
```

## Installation

1. Copy `CC2_V4_0_STOCK_20260920_162601_4ab3dab2.zip.sig` unchanged to USB storage.
2. Do not extract, rename, or modify it.
3. Insert the USB drive and start the update from the printer interface.
4. Do not remove power while the package is being verified, written, or booted.
5. Allow the first boot to complete and wait until the normal printer menus are available.

The printer uses A/B system partitions. The update writes the inactive system slot, which becomes active after reboot. Persistent data under `/opt/usr` is separate from the firmware system image; however, the root password belongs to the system image and is not preserved by this release.

## First-run setup

Open:

```text
http://PRINTER-IP:8081
```

On a clean installation, enter the printer's LAN access code in the browser configurator. The credential is stored locally on the printer and is not included in the public firmware.

After completing first-run configuration, perform one complete printer reboot and wait 30–60 seconds before reopening CC2 Control. This allows Canvas and the printer services to complete their initialization sequence.

## Validation

From SSH:

```sh
pidof cc2-control
wget -qO- http://127.0.0.1:8081/api/health
wget -qO- http://127.0.0.1:7125/server/info
```

Expected results:

- CC2 Control reports version `1.1.22`.
- `mqtt_connected`, `mqtt_registered`, and `snapshot_received` are `true` after setup.
- The running executable is `/opt/inst/cc2-control/cc2-control`.
- The process command line contains `--panda-port 7125`.
- Port 7125 reports `klippy_connected:true` and `klippy_state:"ready"`.

Then verify the dashboard, camera, temperatures, Canvas if installed, Bed Mesh, and the G-code library. Test motion or calibration only with a clear bed and an idle printer.

## Panda Breath

Configure Panda Breath with the printer's IP address and port `7125`. The bridge does not require the CC2 LAN code and exposes read-only printer status only.

## Recovery

Keep the official 02.01.00.00 recovery package available. If the new slot cannot boot, use the printer's documented recovery/update procedure or return to the previously verified A/B slot. Never interrupt power during recovery.

## Warning

Modified firmware can damage or disable a printer. Installation is entirely at the user's risk and no warranty is provided.
