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

## Idle motor release

The CC2 firmware's `idle_timeout` only switches the heaters off; unlike stock Klipper it never releases the steppers. After homing, calibration or jogging they stay energised, and the mainboard fan (`controller_fan board_cooling_fan`, which runs while any stepper is enabled) keeps spinning indefinitely although the part, auxiliary and chamber fans read 0 %. On V4.2-R5 the fan was still at 100 % (about 6 700 rpm) half an hour after a Bed Mesh calibration, and `M84` stopped it at once.

CC2 Control therefore sends `M84` once the printer has stood Idle for ten minutes with both heater targets at zero, no movement and no console command running, like Klipper's default `idle_timeout`. Printing, a paused print, any other machine state or stale telemetry restarts the wait, and the release is sent once per stationary period. Afterwards the axes must be homed again before manual moves.

## Settings and themes

Settings are split into compact Connection, Safety, Integrations, Appearance and About panels.

CC2 Control keeps the original Dark theme and adds a persistent monochrome **Light theme**. The selected appearance is shared between browser and OrcaSlicer.

## OrcaSlicer Device integration

CC2 Control can be loaded directly in OrcaSlicer as the printer Device page. V4.2 supports the same dashboard and control UI there, together with the upload and print workflow.

For Canvas prints, the spool-selection popup can also appear inside the OrcaSlicer Device view without requiring a manual refresh.

Uploads never overwrite a file. When OrcaSlicer sends a name that already exists, for example after re-slicing the same model, CC2 Control saves the upload as `name (1).gcode`, then `name (2).gcode` and so on, up to 99 copies. An upload-and-print then confirms that copy. Browser uploads still refuse an existing name, so the operator can choose another one.

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

The Job page asks for objects only while a print is active. The excluded and current objects come from the UDS telemetry subscription. The object list is asked once per job; an empty list is asked again after a growing delay, because objects are defined only when the G-code starts. The printer's firmware keeps every request on its local socket in memory, so this route must not query it on every poll (see [UDS telemetry](UDS_TELEMETRY.md)).

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

**Canvas → Canvas controls → Auto refill** shows and changes the Canvas setting that continues a print from another slot holding the same filament when a spool runs out (vendor method 2004). The switch appears only after the printer has reported the setting, and it changes only when the printer reports the new value.

The G-code library lists printable files from internal memory and USB storage, including nested folders and thumbnails where available. USB jobs are validated and imported before the printer's native print-start workflow is used.

## Printer sub-states and refusals

Next to the machine state, the dashboard, Job and Control pages and the top bar (on wider screens) show what the printer is doing within it, for example **Printing · Heating bed**, **Printing · Paused** or **Manual homing · Failed**. The meaning of each vendor sub-state code depends on the state, as in ELEGOO's SDK.

Requests CC2 Control sends over MQTT (print start, Canvas auto refill, history, time-lapse rendering) are answered by the printer after the HTTP request has returned. When the printer refuses one, every open page shows the reason once, for example *Print start refused by the printer: the printer is busy (code 1009)*.

## Print history and time-lapse videos

**History** lists the print jobs recorded by the printer, newest first, with start time, duration, result and time-lapse. Opening the page or pressing **Refresh** sends one request (vendor method 1036); nothing polls the printer in the background.

- **Download** streams a rendered time-lapse MP4 from the printer through CC2 Control; the browser never receives the LAN access code.
- **Create video** renders the stored frames of a job into an MP4 (vendor method 1051). It is available only while the printer is Idle, one video at a time, and keeps the printer busy for several minutes.

The printer lists its last 50 jobs (about 20 KB on the tested printer); replies larger than 256 KiB would not be loaded.

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

### Timelapse selection and history deletion

Select **Enable timelapse** in the print popup for each job that should record frames.
After completion, use **Create video** in History if the printer reports unrendered frames,
then **Download** when the MP4 is ready. Saved-mesh and calibrated starts both send an explicit
recording choice; this build is intended for printer validation of both paths.

History offers confirmed deletion of one completed/stopped record or all completed/stopped records
in the loaded list (the printer currently returns its last 50 jobs). Refresh first if the cached list
is older than one minute. Deletion is unavailable during printing or video generation. It removes
history records, not G-code or video files; download any video you want before deleting its record.

Starting live camera view in another CC2 Control tab/device hands it over on the previous page's next
printer-state poll. **Watch here** takes it back explicitly. The camera window follows the same rule.
Failed ownership requests leave streaming stopped. Direct Elegoo clients remain independent.
