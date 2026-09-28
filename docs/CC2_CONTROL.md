# CC2 Control

CC2 Control is the lightweight local control platform for the Centauri Carbon 2 Community Firmware. Community Firmware **V4.2** originally ships CC2 Control **1.1.30**; **1.1.31** is the current standalone CC2 Control update and source line. It runs directly on the printer and serves a dependency-free web interface on TCP port **8081**.

Open:

```text
http://PRINTER-IP:8081
```

The same interface can be used inside the **OrcaSlicer Device tab**.

A clean installation starts with a browser configurator that requests the printer LAN access code. Credentials remain on the printer.

After firmware installation, wait for hardware initialisation before entering the LAN access code. First-run registration realigns CC2 Control, MQTT and Canvas services automatically; an additional manual power cycle is not needed.

## Dashboard and protected controls

The dashboard combines the live camera, print state and progress, temperatures, browser-rendered thermal history, fan speeds, position, hardware state, memory, uptime, load and MQTT status.

Protected actions include jogging and homing, live Z-offset adjustment, temperature and fan targets, pause/resume/cancel, light, speed, flow, extrusion, motors, heaters and emergency stop. State checks reject inappropriate actions while the printer is busy.

## Bed Levelling workflow

CC2 Control retains the unified Bed Mesh, saved Side A / Side B profiles, active mesh state and four-screw measurement under one **Bed Levelling** workflow.

Four-screw corrections are displayed in **microns**. A guarded **Optimized reference adjustment** suggestion may be shown when it reduces the largest required correction and remains inside the protected movement range. The suggestion is advisory only; CC2 Control does not move or turn screws automatically.

## Quick Actions and global Emergency Stop

The dashboard now provides operational Quick Actions for Home All, All Heaters Off, Fans Off and Motors Off.

A global Emergency Stop remains accessible from every page and requires a deliberate one-second press-and-hold before execution.

## Settings and themes

Settings are split into compact Connection, Safety, Integrations, Appearance and About panels.

CC2 Control keeps the original Dark theme and adds a persistent monochrome **Light theme**. The selected appearance is shared between browser and OrcaSlicer.

## OrcaSlicer Device integration

CC2 Control can be loaded directly in OrcaSlicer as the printer Device page. V4.2 supports the same dashboard and control UI there, together with the upload and print workflow.

For Canvas prints, the spool-selection popup can also appear inside the OrcaSlicer Device view without requiring a manual refresh.

## LAN access-code revalidation

If the LAN access code is changed on the printer, open **Settings → Connection**, enter the replacement code and select **Change / Revalidate**. CC2 Control writes the new credential atomically and restarts only its own service so MQTT, Canvas and snapshot state can be established again without rebooting or power-cycling the printer.

The SSH restart and configuration-reset commands documented in [Installation](INSTALL.md) remain available as recovery fallbacks.

## OrcaSlicer Canvas filament synchronization

CC2 Control adds optional support for OrcaSlicer's **Moonraker** printer agent filament synchronization. The read-only endpoints on port `8081` are:

```text
GET /server/info
GET /server/database/item?namespace=lane_data
```

The lane-data response is built from the cached Canvas snapshot and maps tray ID, material, colour and maximum nozzle temperature into the format expected by OrcaSlicer. The existing Octo/Klipper workflow, Device tab, upload and Canvas print-confirmation flow remain unchanged.

Thanks to **@efiten** for the original proposal, analysis and printer-side validation.

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

CC2 Control exposes a limited Moonraker-compatible service on TCP port **7125** for integrations such as BTT Panda Breath.

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

V4.2 includes a stable system/capability discovery API intended for external integrations. It identifies Community Firmware, CC2 Control, service ports and supported capabilities without exposing the printer access code or other credentials.

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

A healthy updated system reports the installed CC2 Control version and reaches healthy MQTT/snapshot state after setup.

## Acknowledgements

Thanks to **Barry Green** for extensive remote hardware testing and feedback during the CC2 Control development cycle.
