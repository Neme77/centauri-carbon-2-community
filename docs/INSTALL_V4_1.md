# Installing Centauri Carbon 2 Community Firmware V4.1

## Scope

This guide applies only to the **ELEGOO Centauri Carbon 2** and Community Firmware **V4.1**, built and tested against official ELEGOO firmware **02.01.00.00**.

Do not install this package on the original Centauri Carbon or another printer model.

## Before installing

- Finish or cancel any active print and allow the heaters to cool.
- Keep the printer connected to reliable power throughout the update.
- Keep the official recovery firmware and the previous known-good community release available.
- Record the printer IP address and LAN access code.
- If you changed the SSH root password, expect to set it again after a full firmware installation.
- Verify the firmware filename and SHA-256.

PowerShell:

```powershell
Get-FileHash .\CC2_V4_1_STOCK_20260926_015216_446f1846.zip.sig -Algorithm SHA256
```

Expected SHA-256:

```text
868814647d3835c193f0f0625e9037545a5a5128ee504ce74c62fbc4c4f41e19
```

## Installation

1. Copy `CC2_V4_1_STOCK_20260926_015216_446f1846.zip.sig` unchanged to USB storage.
2. Do not extract, rename or modify it.
3. Insert the USB drive and start the update from the printer interface.
4. Do not remove power while the package is being verified, written or booted.
5. Allow the first boot to complete and wait until the normal printer menus are available.
6. **Reboot the printer once more.**
7. After that reboot, wait roughly 30–60 seconds before opening CC2 Control.

> [!IMPORTANT]
> The extra reboot in step 6 is part of the V4.1 installation procedure. It allows CC2 Control, Canvas state and background services to initialize cleanly before normal use.

The printer uses A/B system partitions. The update writes the inactive system slot, which becomes active after reboot. Persistent data under `/opt/usr` is separate from the firmware system image.

## CC2 Control first-run setup

Open:

```text
http://PRINTER-IP:8081
```

On a clean installation, enter the printer's LAN access code in the browser configurator. The credential is stored locally on the printer and is not included in the public firmware.

The same page can also be configured as the **Device** page in OrcaSlicer.

## Validation

From SSH:

```sh
pidof cc2-control
readlink /proc/$(pidof cc2-control)/exe
wget -qO- http://127.0.0.1:8081/api/health
wget -qO- http://127.0.0.1:7125/server/info
wget -qO- http://127.0.0.1:7125/printer/info
```

Expected:

- CC2 Control reports version `1.1.25`
- the executable is running from `/opt/inst/cc2-control/cc2-control`
- port 8081 serves the CC2 Control interface
- port 7125 is listening
- `/server/info` and `/printer/info` return valid JSON
- MQTT and snapshot state become healthy after setup

Then verify:

- dashboard
- live camera
- temperatures and fans
- printer controls
- Canvas, if installed
- Bed Mesh
- G-code file manager
- OrcaSlicer Device view
- upload / upload-and-print workflow

Test movement or calibration only with a clear bed and an idle printer.

## Recovery

Keep the official **02.01.00.00** recovery package available. If a new slot cannot boot, use the printer's documented recovery/update procedure or return to the previously verified A/B slot.

Never interrupt power during recovery.

## Warning

Modified firmware can damage or disable a printer. Installation is entirely at the user's risk and no warranty is provided.
