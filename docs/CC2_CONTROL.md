# CC2 Control 1.1.14

CC2 Control is a lightweight local control service integrated into Centauri
Carbon 2 Community Firmware v3.9. It runs directly on the printer and serves a
dependency-free web interface on TCP port 8081.

Open:

```text
http://PRINTER-IP:8081
```

## Dashboard

![Dashboard overview](images/v3.9/dashboard-overview.jpg)

The dashboard combines the live camera, printer status, current job, time,
temperatures, short browser-side thermal history, fans, position, hardware
state, system memory, uptime, load and MQTT message count.

## Protected controls

![Printer controls](images/v3.9/printer-controls.jpg)

Available controls include:

- protected jogging and homing;
- live session Z-offset adjustment;
- nozzle and bed targets;
- part, auxiliary and case fans;
- pause, resume and cancel;
- light, speed and flow controls;
- safe manual extrusion and retraction;
- motors off, all off and emergency stop.

Backend validation checks ranges, printer state and required homing before a
command is generated. Potentially dangerous operations require confirmation.

## Materials and calibration

![Materials and guided calibration](images/v3.9/materials-and-calibrations.jpg)

PLA, PETG, ABS, ASA, TPU and PA-CF presets are included. Users can add or update
safe custom names such as PLA+, ASA-CF or PA-CF. Presets are stored on the
printer in `/opt/usr/cc2-control/material-presets.json` and are therefore
available to every browser.

Guided controls are included for nozzle PID, bed PID, input shaper and bed mesh.

## Protected console

![Protected console](images/v3.9/protected-console.jpg)

The console is not an unrestricted shell or arbitrary G-code endpoint. It uses
an allowlist, blocks multiline input, restricts calibration while printing and
does not expose low-level firmware restart or automatic `SAVE_CONFIG` actions.

## Bed Mesh 2D

![Bed Mesh heatmap](images/v3.9/bed-mesh-2d-overview.jpg)

![Bed Mesh table](images/v3.9/bed-mesh-2d-table.jpg)

CC2 Control reads the current Klipper bed mesh through the printer's protected
local UDS interface. The 11×11 result is rendered as summary statistics, a
colour heatmap and a complete numerical table.

## Bed Mesh 3D

![Interactive Bed Mesh](images/v3.9/bed-mesh-3d-overview.jpg)

![Bed Mesh Cartesian reference](images/v3.9/bed-mesh-3d-detail.jpg)

The browser renders the same mesh as an interactive surface with drag rotation,
wheel or pinch zoom and adjustable Z exaggeration. The ideal Z=0 plate,
Cartesian X/Y/Z axes and vertical displacement stems provide a real spatial
reference without adding a continuous animation loop to the printer.

## ELEGOO Canvas

![Four-slot Canvas control](images/v3.9/canvas-four-slot-control.jpg)

Canvas is discovered through the printer's native MQTT method 2005. The latest
four-tray state is retained independently from normal temperature updates.
CC2 Control displays material, colour, brand, nozzle range, active tray and
module state. Protected controls support slot selection, load, unload and
material metadata changes while the printer is idle.

Cold-boot discovery retries automatically and stops after valid `canvas_info`
telemetry is received.

## First-run setup

Version 1.1.14 adds a browser-based first-run flow. It appears only when the
service starts without complete MQTT credentials. The access code is written
atomically with file mode `600`; after successful printer registration the
setup endpoint locks. The SSH command `cc2-configure` remains available as a
recovery path.

## Persistent and firmware-owned paths

Firmware-owned application:

```text
/opt/inst/cc2-control
```

Persistent private state:

```text
/opt/usr/cc2-control/cc2-control.conf
/opt/usr/cc2-control/material-presets.json
```

## Local API

| Endpoint | Purpose |
| --- | --- |
| `GET /api/health` | Service version, resources and MQTT state |
| `GET /api/setup` | First-run state without returning credentials |
| `POST /api/setup` | First-run access-code configuration |
| `GET /api/printer` | Accumulated printer telemetry |
| `GET /api/canvas` | Retained Canvas telemetry |
| `POST /api/canvas/refresh` | Request Canvas method 2005 state |
| `GET /api/material-presets` | Printer-persistent presets |
| `PUT /api/material-presets` | Validate and save presets |
| `GET /api/mesh` | Current protected bed-mesh query |
| `GET /api/console` | Protected console status and output |
| `POST /api/console/command` | Submit one allowlisted command |
| `POST /api/control` | Submit one structured dashboard action |

These APIs also provide a practical path for future Home Assistant integration.

