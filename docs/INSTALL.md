# Installation

Always install artifacts and follow instructions from the same GitHub release.

## Full firmware

1. Verify the downloaded firmware checksum against the release checksum file.
2. Install the signed package using the printer's supported update procedure.
3. Allow the normal update reboot to complete.
4. Wait until the printer has completed hardware initialisation.
5. Open `http://PRINTER-IP:8081` and enter the printer LAN access code.
6. Allow several seconds for CC2 Control to register and realign its services.

V4.2 with CC2 Control 1.1.30 does not require an additional manual power cycle. If the printer or Canvas is not ready, wait for initialisation to finish before attempting control actions.

## CC2 Control-only update

Use the multiplatform updater attached to the relevant release. It supports Windows, Linux and macOS over SSH and preserves persistent settings unless the release notes explicitly say otherwise.

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
