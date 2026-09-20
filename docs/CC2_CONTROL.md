# CC2 Control 1.1.22

CC2 Control is a lightweight local control platform integrated into Centauri Carbon 2 Community Firmware V4.0. It runs directly on the printer and serves a dependency-free web interface on TCP port 8081.

Open `http://PRINTER-IP:8081`. A clean installation starts with a browser configurator that requests the printer LAN access code. Credentials remain on the printer.

## Dashboard and protected controls

The dashboard combines the live camera, print state and progress, temperatures, browser-rendered thermal history, fan speeds, position, hardware state, memory, uptime, load, and MQTT status.

Protected actions include jogging and homing, live Z-offset adjustment, temperature and fan targets, pause/resume/cancel, light, speed, flow, extrusion, motors, heaters, and emergency stop. State checks reject inappropriate actions while the printer is busy.

## Four-screw load-cell bed tramming

The guided screw measurement probes directly above the four bed screws:

| Screw | Coordinate | Role |
| --- | --- | --- |
| Front left | X35 Y30 | Reference |
| Front right | X225 Y30 | Relative correction |
| Rear right | X225 Y225 | Relative correction |
| Rear left | X35 Y225 | Relative correction |

Each point uses three load-cell probe samples. CC2 Control homes the printer first, requires the printer to be idle, and reports each screw's difference from the front-left reference with raise/lower guidance. It does not run `SAVE_CONFIG` or alter the stored Bed Mesh.

## Bed Mesh 2D and 3D

The Bed Mesh view displays the printer's full 11×11 mesh with minimum, maximum, range, average, a 2D value map, and an interactive 3D surface. V4.0 corrects the X-axis orientation. Screw-measurement output is captured separately and can no longer replace valid mesh geometry with four probe points.

## Object exclusion

For labelled multi-object G-code, CC2 Control displays the detected print objects and can send Klipper `EXCLUDE_OBJECT` for a selected component. The action is protected by confirmation and is irreversible for the current print. Availability depends on object labels being present in the sliced G-code.

## Temporary expert terminal unlock

The console remains protected by default. Arbitrary G-code requires two warnings and the exact phrase `UNLOCK GCODE`. A random in-memory authorization token is then valid for five minutes and is never stored.

Expert commands are accepted only while the printer is idle. Movement commands (`G0`, `G1`, `G2`, and `G3`) are additionally rejected until X, Y, and Z have all been homed. Emergency and protected dashboard controls remain available independently.

> Expert mode can move hardware, heat components, or damage the printer. Review every command before sending it.

## Panda Breath bridge

CC2 Control 1.1.22 exposes a deliberately limited Moonraker-compatible endpoint on TCP port 7125 for BTT Panda Breath. It supplies connection state, print state and progress, and nozzle/bed temperatures and targets over HTTP/WebSocket.

The bridge is read-only: it does not expose G-code execution, file operations, heater controls, or motion controls. It was hardware-tested with Panda Breath firmware 1.0.4. Configure Panda Breath with the printer IP and port `7125`; no separate Moonraker access code is required.

Checks:

```sh
wget -qO- http://127.0.0.1:7125/server/info
wget -qO- http://127.0.0.1:7125/printer/objects/list
```

## Canvas, material presets, and G-code library

CC2 Control retains native four-slot ELEGOO Canvas discovery and protected load, unload, material-editing, and spool-mapping workflows. User-defined material presets are stored under `/opt/usr`.

The G-code library lists printable files from internal memory and USB storage, including nested folders. USB jobs are validated, atomically imported into internal storage, and started using the printer's proven native method 1020 workflow; the source file is not modified.

## Runtime and health checks

CC2 Control is a statically linked ARM process managed by `procd`. In Firmware V4.0, the integrated executable resides under `/opt/inst/cc2-control`, while persistent configuration and presets reside under `/opt/usr/cc2-control`.

```sh
wget -qO- http://127.0.0.1:8081/api/health
wget -qO- http://127.0.0.1:8081/api/setup
wget -qO- http://127.0.0.1:8081/api/canvas
```

A healthy configured system reports version `1.1.22`, `mqtt_connected:true`, `mqtt_registered:true`, and `snapshot_received:true`.

## Acknowledgements

Thanks to **Barry Green** for extensive remote hardware testing and feedback, including Panda Breath integration, object exclusion, and the four-screw calibration workflow.
