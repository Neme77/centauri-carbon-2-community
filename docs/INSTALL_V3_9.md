# Installing Centauri Carbon 2 Community Firmware v3.9

## Scope

This guide applies only to the **ELEGOO Centauri Carbon 2** and to the v3.9
community firmware built and tested against ELEGOO firmware 02.01.00.00.

Do not use this package on the original Centauri Carbon or another printer.

## Before installing

- Finish or cancel any active print.
- Remove filament operations and allow heaters to cool.
- Keep the printer connected to reliable power.
- Save the official recovery firmware and the previous known-good community release.
- Record the printer IP address and LAN access code.
- Verify that the downloaded firmware name and SHA-256 match the release page.

PowerShell checksum command:

```powershell
Get-FileHash .\CC2_V3_9_STOCK_20260918_190118_8f964542.zip.sig -Algorithm SHA256
```

Expected value:

```text
f23de6c835f863ba3cc93ab98b9be6dc90fc6c8f7a6aa17ad5624ca48120a6c5
```

## Install from USB

1. Copy `CC2_V3_9_STOCK_20260918_190118_8f964542.zip.sig` to the USB drive without extracting or renaming it.
2. Insert the drive while the printer is idle.
3. Start the update from the printer's local update interface.
4. Do not remove USB storage or interrupt power while the update is being written.
5. Allow the printer to reboot and finish its complete hardware initialisation.

The CC2 uses A/B root filesystems. The update is written to the inactive slot;
persistent user data remains under `/opt/usr`.

## Configure CC2 Control

Wait 60–90 seconds after the first boot, then open:

```text
http://PRINTER-IP:8081
```

On a clean installation, the first-run page asks for the LAN access code shown
by the printer. The browser sends it directly to CC2 Control using a local POST
request. It is written only to:

```text
/opt/usr/cc2-control/cc2-control.conf
```

The file is protected with mode `600`. The code is not placed in the URL,
returned by the API or stored in browser local storage. After successful MQTT
registration, the first-run endpoint locks automatically.

If an existing valid configuration is found, the dashboard opens immediately.

### SSH recovery setup

If browser setup cannot be completed, connect through SSH and run:

```sh
cc2-configure
```

## Required post-install reboot

> **Do not skip this step on a clean installation.**

After completing first-run configuration, perform one complete reboot of the
printer. During real-hardware validation, CC2 Control could already see Canvas
after the firmware update while the original printer UI had not yet refreshed
its Canvas menus. A normal reboot synchronised both interfaces, after which
Canvas remained available in both.

Wait another 60–90 seconds after reboot before testing the dashboard.

## Verification

Through SSH:

```sh
pidof cc2-control

PID=$(pidof cc2-control)
readlink /proc/$PID/exe

wget -qO- http://127.0.0.1:8081/api/health
wget -qO- http://127.0.0.1:8081/api/setup
wget -qO- http://127.0.0.1:8081/api/canvas

ls -l /opt/usr/cc2-control/cc2-control.conf
```

Expected executable:

```text
/opt/inst/cc2-control/cc2-control
```

Expected health fields:

```json
{
  "service": "cc2-control",
  "version": "1.1.14",
  "mqtt_connected": true,
  "mqtt_registered": true,
  "snapshot_received": true
}
```

Expected setup state:

```json
{
  "required": false,
  "configured": true,
  "mqtt_connected": true,
  "mqtt_registered": true,
  "snapshot_received": true
}
```

With a connected Canvas, `/api/canvas` should report `"available":true` and four trays.

## Updating an existing installation

The following files are persistent and are not embedded or overwritten by the
root filesystem image:

```text
/opt/usr/cc2-control/cc2-control.conf
/opt/usr/cc2-control/material-presets.json
```

The firmware-provided application and web interface are installed under:

```text
/opt/inst/cc2-control
```

## Network safety

CC2 Control is intended for a trusted local network. Its protected console
restricts printer commands, but port 8081 is not a public multi-user login
service. Do not forward it directly from a router. Use a VPN or an authenticated
reverse proxy for remote access.

## Recovery

If the printer does not complete a normal boot, stop and use the official
recovery process or the previous known-good firmware. Do not repeatedly power
cycle while an update is actively being written.

If only CC2 Control requires recovery:

```sh
/etc/init.d/cc2-control restart
```

Reconfigure only when necessary:

```sh
cc2-configure
```
