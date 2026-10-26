# Installation

Start with the step-by-step [first setup and LAN-code guide](FIRST_RUN.md) ([Italiano](FIRST_RUN.it.md)). For slicer connection and filament import, see [OrcaSlicer and Canvas sync](ORCASLICER.md) ([Italiano](ORCASLICER.it.md)).

Always install artifacts and follow instructions from the same GitHub release.

## Full firmware

For V4.4, install the [complete signed firmware](https://github.com/Neme77/centauri-carbon-2-community/releases/download/V4.4/CC2_V4_4_STOCK_20261013_001035_414d6629.zip.sig). Verify SHA-256, copy the unchanged `.zip.sig` to the USB root, then use **Settings → Check for Updates → Offline Update** on the printer. Do not unpack it or use the source-builder ZIP as an update. Let installation and reboot complete, then open `http://PRINTER-IP:8081`. A factory reset is not required.

The [English first-installation guide](FIRST_RUN.md) and [Italian guide](FIRST_RUN.it.md) contain the exact filename/checksum, USB steps, LAN-code setup and startup checks.

## CC2 Control-only updates

**CC2 Control 1.2.5 is a separate updater exclusively for existing Community Firmware V4.4 installations.** Follow the [detailed update guide](UPDATE_CC2.md) ([Italiano](UPDATE_CC2.it.md)). Actual printer/daemon fingerprints are checked before modification; do not use it on V4.3 or original ELEGOO firmware. The original V4.4 firmware release retains its original assets; historical application updaters apply only to their documented bases.

## Original firmware and downgrade restriction

[Download original CC2 02.01.01.07 firmware](https://public.suchmeme.nl/cc2-02.01.01.07-1cbee81a985601d1944cea50773793ba-release-abroad.zip.sig). This is an externally hosted mirror of the official package, not an ELEGOO-hosted download. Its bytes match the exact official base pinned by the V4.4 builder.

SHA-256: `278a0606a072ae432e9bf2d4f0157acba4570d1c85dbc768c359fef53ac5f406`.

**ELEGOO blocks downgrades to older firmware.** This link provides the original **same-version 02.01.01.07** package; it does not enable a downgrade to 02.01.00.00 or earlier. Reinstalling the stock package is a separate operation from a factory reset, which does not replace the installed firmware.

Use the same USB/offline-update procedure in the first-installation guide with the original package. File identity has been verified, and the maintainer confirmed a successful return from V4.4 to this same-version original firmware on the printer. If the printer refuses the package, stop and record its message; do not change version metadata or bypass its checks. Original firmware does not include CC2 Control or community features.

## SSH access reference

The Community Firmware provides local SSH access for maintenance:

| Setting | Default |
| --- | --- |
| Username | `root` |
| Password | `MTY4ODE2` |

From PowerShell or a terminal, run `ssh root@PRINTER-IP`, replacing `PRINTER-IP` with the printer’s local IP, and enter the password when prompted. Password entry is not displayed. If you have changed the root password, use your own replacement. This is the SSH password, separate from the printer LAN access code used by CC2 Control. These credentials refer to the Community Firmware; SSH is not required for normal browser setup.

## Changing the printer LAN access code

If the LAN access code is changed from the printer, update the same code in CC2 Control:

1. Open `http://PRINTER-IP:8081`.
2. Go to **Settings → Connection**.
3. Enter the new code in **LAN access code**.
4. Select **Change / Revalidate**.
5. Confirm the replacement.

CC2 Control writes the replacement atomically and restarts only its own service. MQTT, Canvas and snapshot state are then established again without a printer reboot or power cycle.

### Recovery if the old code blocks configuration access

If the previous code prevents access to the configuration workflow entirely, connect over SSH and reset only the CC2 Control connection configuration:

```sh
/etc/init.d/cc2-control stop
rm -f /opt/usr/cc2-control/cc2-control.conf
/etc/init.d/cc2-control start
```

Then reload:

```text
http://PRINTER-IP:8081
```

The initial setup workflow will appear again so the new LAN access code can be entered and verified.

This reset removes only the CC2 Control connection configuration. Material presets and interface preferences remain intact.

## Verification

```sh
wget -qO- http://127.0.0.1:8081/api/health
wget -qO- http://127.0.0.1:8081/api/v1/system/info
wget -qO- http://127.0.0.1:7125/server/info
```

Do not install files from different releases as a mixed set.

## Protected live Z offset

The UI reads `gcode_move.homing_origin[2]` through the shared firmware telemetry
cache; it never substitutes zero for missing readback. Protected adjustments
require fresh readback, a session adjustment within ±0.50 mm of the printer reference, and confirmation of
the preceding adjustment. A browser reload does not reset the guard. Undo
restores the firmware reference captured before the first accepted session adjustment and does not write saved firmware settings.

If the firmware does not report this field, the adjustment buttons remain
disabled. Validate field availability and existing print/calibration offset
behaviour on the printer before releasing this change. An unconfirmed command
keeps further adjustments blocked; inspect the firmware state before restarting
CC2 Control to re-establish readback. This guard applies to protected CC2 Control
actions, not arbitrary G-code explicitly entered in the expert console.

Live adjustments use Klipper `SET_GCODE_OFFSET Z_ADJUST=... MOVE=1 MOVE_SPEED=5`,
including during printing, with Z homed. The backend owns the reference across
browser reloads and clients. A confirmed external offset change establishes a new
reference; an unconfirmed command stays blocked. Restarting CC2 Control starts
a new session from current readback. Nothing is written using SAVE_CONFIG.

## Browser host and origin protection

CC2 Control accepts its local IP address, the printer hostname (also with `.local`),
and loopback `localhost`, with the HTTP service port. OrcaSlicer remains supported. Mutating API requests require the
`X-CC2-Request: 1` header supplied by the bundled UI; CLI clients must supply
this header too. Native Orca uploads to `/api/files/local` are the sole exception
and still require operator confirmation before printing. Browser commands also
require the same origin. Cross-site requests are
rejected; the service does not enable CORS.

For a trusted DNS alias, add `--http-host printer.example.local` to the CC2
Control launcher arguments. Supply only the hostname, without a scheme or port.
This setting does not provide authentication for other clients on the LAN.

HTTP reception uses eight bounded request slots and a two-second total receive
deadline. Incomplete headers or small command bodies do not monopolize the
MQTT/UDS loop. Upload bodies continue in their existing bounded workers. This
does not make the emergency control a substitute for the printer's physical
stop: response transmission and firmware operations can still take time.
