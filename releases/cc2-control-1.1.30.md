# CC2 Control 1.1.30

CC2 Control **1.1.30** completes the V4.2 print, mesh and live-status workflow
and replaces the old manual post-install restart procedure.

## Highlights

- Side A and Side B map to their saved `default` and `default1` profiles;
- a single calibration option follows the printer's native print workflow;
- adaptive G-code probing and full-bed fallback are selected automatically;
- live job data no longer falls back to demonstration values;
- elapsed time, remaining time, total layers and object statistics use live data;
- the firmware service starts from `/opt/inst/cc2-control/start.sh`;
- first-run LAN-code registration restarts CC2 Control automatically;
- MQTT, Canvas and snapshot state reconnect without restarting printer services.

## No printer restart required

After a normal installation or update, an additional manual reboot or full
power-off/power-on cycle is no longer required. If first-run synchronisation
takes longer than expected, wait roughly 30 seconds and refresh the page once.

## Release package

```text
CC2-Control-1.1.30-Source-and-Multiplatform-Builder.zip
```

SHA-256:

```text
0ed1834990070135740a7205758308701c762d6fab20bff28891d65457bbdb97
```

The archive includes source code, web UI, tests, firmware-integration files and
the multiplatform package builder.
