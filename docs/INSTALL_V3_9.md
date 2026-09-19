# Installing Centauri Carbon 2 Community Firmware v3.9

## Scope

This guide applies only to the **ELEGOO Centauri Carbon 2** and to Community Firmware v3.9 built and tested against official ELEGOO firmware **02.01.00.00**.

Do not install this package on the original Centauri Carbon or any other printer model.

## Before installing

- Finish or cancel any active print and allow the heaters to cool.
- Keep the printer connected to reliable power throughout the update.
- Keep the official recovery firmware and the previous known-good community release available.
- Record the printer IP address and LAN access code.
- Verify the firmware filename and SHA-256 against the release page.

PowerShell verification:

```powershell
Get-FileHash .\CC2_V3_9_STOCK_20260919_172158_4f40c471.zip.sig -Algorithm SHA256
```

Expected SHA-256:

```text
332af63c3eb5fbfb3e252aa2941c96fc6f152bb35b23810ea72666b7b7a09e64
```

## Installation

1. Copy `CC2_V3_9_STOCK_20260919_172158_4f40c471.zip.sig` unchanged to USB storage.
2. Do not extract, rename or modify the package.
3. Insert the USB drive and start the update from the printer interface.
4. Do not remove power while the package is being verified, written or booted.
5. Allow the first boot to finish completely and wait until the normal printer menus are available.

The printer uses A/B system partitions. The update writes the inactive system slot and the new slot becomes active after reboot. Persistent user data under `/opt/usr` is separate from the firmware system image.

## First-run CC2 Control setup

Open:

```text
http://PRINTER-IP:8081
```

On a clean installation, follow the browser configurator and enter the local printer/MQTT credentials requested by the page. Credentials remain on the printer and are not included in the public firmware.

Existing CC2 Control configuration and custom material presets under `/opt/usr` are preserved during normal A/B upgrades.

## Mandatory reboot after setup

> After completing the first-run browser configuration, perform one complete printer reboot—even if Canvas already appears on the touchscreen or dashboard.

During a cold boot, Canvas may transition from red to white, briefly flash red while the printer services initialize, and finally return to white. CC2 Control retries discovery during this sequence. The additional complete reboot ensures that Canvas is synchronized with both CC2 Control and the original touchscreen interface.

After the reboot, wait 30–60 seconds before opening the dashboard.

## Validation

From SSH:

```sh
pidof cc2-control
wget -qO- http://127.0.0.1:8081/api/health
wget -qO- http://127.0.0.1:8081/api/setup
wget -qO- http://127.0.0.1:8081/api/canvas
```

Expected health indicators:

- `version` is `1.1.17`;
- `mqtt_connected` is `true`;
- `mqtt_registered` is `true`;
- `snapshot_received` is `true`;
- Canvas reports `available:true` when the module is connected.

Then verify from the browser:

1. live dashboard and camera;
2. temperature chart remains a fixed height;
3. Canvas shows all four trays;
4. internal-memory printing starts with the selected spool mapping;
5. USB printing works from both the drive root and a nested folder.

## Recovery and rollback

Keep the official 02.01.00.00 recovery package available. If the new slot cannot boot, use the printer's documented recovery/update procedure or return to the previously verified A/B slot. Do not interrupt power during recovery.

## Warning

Modified firmware can damage or disable a printer. Installation is entirely at the user's risk and no warranty is provided.
