# CC2 Control 1.1.25

CC2 Control is the lightweight local control platform integrated into Centauri Carbon 2 Community Firmware **V4.1**. It runs directly on the printer and serves a dependency-free web interface on TCP port **8081**.

Open:

```text
http://PRINTER-IP:8081
```

The same interface can be used inside the **OrcaSlicer Device tab**.

A clean installation starts with a browser configurator that requests the printer LAN access code. Credentials remain on the printer.

> [!IMPORTANT]
> After installing Community Firmware V4.1, reboot the printer once after the first successful boot before normal use.

## Dashboard and protected controls

The dashboard combines the live camera, print state and progress, temperatures, browser-rendered thermal history, fan speeds, position, hardware state, memory, uptime, load and MQTT status.

Protected actions include jogging and homing, live Z-offset adjustment, temperature and fan targets, pause/resume/cancel, light, speed, flow, extrusion, motors, heaters and emergency stop. State checks reject inappropriate actions while the printer is busy.

## OrcaSlicer Device integration

CC2 Control can be loaded directly in OrcaSlicer as the printer Device page. V4.1 supports the same dashboard and control UI there, together with the upload and print workflow.

For Canvas prints, the spool-selection popup can also appear inside the OrcaSlicer Device view without requiring a manual refresh.

## Four-screw load-cell bed tramming

The guided screw measurement probes above the four bed screws and reports each point relative to the reference corner. Measurements use the printer load-cell probe and remain isolated from the stored Bed Mesh.

The operation requires the printer to be idle and performs homing before probing. It does not run `SAVE_CONFIG`.

## Bed Mesh 2D and 3D

The Bed Mesh view displays the printer's full 11×11 mesh with minimum, maximum, range, average, a 2D value map and an interactive 3D surface.

Screw-measurement results are captured separately and cannot replace valid mesh geometry.

## Object exclusion

For labelled multi-object G-code, CC2 Control displays detected print objects and can send Klipper `EXCLUDE_OBJECT` for a selected component.

The action is protected by confirmation and is irreversible for the current print. Availability depends on object labels being present in the sliced G-code.

## Protected expert console

The console is protected by an explicit unlock/confirmation workflow and is blocked while printing.

The backend enforces additional safeguards:

- `G28` is allowed before homing
- `G0/G1` movement is rejected until X/Y/Z are homed
- `G90/G91` mode is tracked
- motion bounds are checked
- low-level movement and stepper-bypass commands are blocked

Klipper remains the final authority for command execution.

> Expert commands can move hardware, heat components or damage the printer. Review every command before sending it.

## Panda / Moonraker-like bridge

CC2 Control 1.1.25 exposes a limited Moonraker-compatible service on TCP port **7125** for integrations such as BTT Panda Breath.

Useful checks:

```sh
wget -qO- http://127.0.0.1:7125/server/info
wget -qO- http://127.0.0.1:7125/printer/info
wget -qO- http://127.0.0.1:7125/printer/objects/list
```

The bridge also exposes object queries and WebSocket compatibility required by supported clients.

## Canvas, material presets and G-code library

CC2 Control supports native four-slot ELEGOO Canvas discovery and protected load, unload, material-editing and spool-mapping workflows.

User-defined material presets are stored under `/opt/usr/cc2-control`.

The G-code library lists printable files from internal memory and USB storage, including nested folders and thumbnails where available. USB jobs are validated and imported before the printer's native print-start workflow is used.

## Discovery API v1

V4.1 includes a stable system/capability discovery API intended for external integrations. It identifies Community Firmware, CC2 Control, service ports and supported capabilities without exposing the printer access code or other credentials.

## Runtime and health checks

Firmware payload:

```text
/opt/inst/cc2-control
```

Persistent configuration:

```text
/opt/usr/cc2-control
```

Health checks:

```sh
wget -qO- http://127.0.0.1:8081/api/health
wget -qO- http://127.0.0.1:8081/api/setup
wget -qO- http://127.0.0.1:8081/api/canvas
```

A healthy configured system reports CC2 Control **1.1.25** and reaches healthy MQTT/snapshot state after setup.

## Acknowledgements

Thanks to **Barry Green** for extensive remote hardware testing and feedback during the CC2 Control development cycle.
