# CC2 Control

CC2 Control is the lightweight local control platform for the Centauri Carbon 2 Community Firmware. It runs directly on the printer and serves a local web interface on TCP port **8081**. Historically, Community Firmware V4.2 shipped CC2 Control 1.1.30; the complete **V4.4.1 firmware** includes CC2 Control **1.2.10** and its signed application updater. Compatible V4.4/V4.4.1 installations can receive newer application-only updates without replacing printer firmware. See the [project README](../README.md) and [changelog](../CHANGELOG.md) for the current application release and firmware compatibility.

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

**Build Plates** maintains named plate profiles with Side A / Side B association, saved mesh measurements at different bed temperatures, and per-plate Z offsets. You can create a profile **before its first calibration** and optionally import an already stored side mesh. A saved measurement may be selected by plate, side and temperature; **Mount and activate** requires confirmation of the physically installed plate. CC2 Control verifies the actual active mesh profile, matrix and bounds before reporting success. If the side-slot profile must be rewritten, a separate confirmation requests a printer restart. Simply viewing a saved mesh or selecting a graph does **not** activate it on the printer.

The plate Z offset is applied on top of the printer's own plate offset and again after every restart. When nozzle profiles are configured, the selected nozzle's Z correction is also applied. Tune the plate offset with Live Z Offset in CC2 Control rather than on the printer screen: the screen's Z offset setting does not know the plate offset, and its first press can replace it. See [the plate/API details](../cc2-control/docs/API.md) for the exact behaviour.

Four-screw corrections are displayed in **microns**. A guarded **Optimized reference adjustment** suggestion may be shown when it reduces the largest required correction and remains inside the protected movement range. The suggestion is advisory only; CC2 Control does not move or turn screws automatically.

**Screw Levelling** also offers an interactive session: **Start session** homes all axes (`G28`), preheats the bed and cleans the nozzle like the one-shot measurement, then probes the four screws and switches the nozzle off, leaving the bed at 60 °C. After that, each corner can be re-probed with one click (`G28 Z` only, after a 30 s settle; the nozzle is switched off again afterwards), and **Check all four** repeats the sweep without a full re-home. Re-probe and check stay disabled until X and Y are homed. Heaters are switched off when the session ends. The original **Measure Screws** one-shot path is unchanged.

**Mesh-guided screw alignment** (the first card on **Screw Levelling**) reads a whole bed mesh instead of four probes. It fits a least-squares plane `z = a + b·x + c·y` through every point of the chosen mesh (the active mesh, a side slot or a saved plate measurement) and shows three things apart:

- the **tilt** in X and Y over a configurable span (240 mm by default), which is what the screws can correct;
- the **correction at each screw** in millimetres, taken from the plane at the screw positions and relative to the screw that needs the least total movement (a positive value means "raise the bed at this corner"). The screws are closer together than the span, so these values are smaller than the tilt;
- the **plate shape** that remains (mesh minus plane) as a heat map with its range, RMS and a name (dome, bowl, saddle, nearly flat or irregular), which the screws cannot change.

"Aligned" is reported only when both tilts are below the target (0.05 mm by default). A plate shape with a range above 0.3 mm gets a warning that neither ends nor blocks the alignment. Target and warning limit are estimates and can be changed, like the span, the screw positions and the row order of the mesh. Turns are shown only when a thread pitch and its source are set, the turning direction only when the direction and its source are set; nothing is assumed. The analysis is read-only and sends nothing to the printer. Its settings are kept in the browser.

The same card offers a **guided alignment**: measure the mesh, turn the screws as listed, measure again, until both tilts are below the target or the configured rounds (4 by default) are used up, then save the mesh to a plate of the plate library. Each round is the bed mesh calibration of the Mesh tab (heat, hold the bed for the soak time, `BED_MESH_CALIBRATE`); no shorter probing command is confirmed for this firmware, so every round measures the full mesh and the last round's mesh is the one saved. Every step first lists the printer commands it will send and sends them only after confirmation. The session runs on CC2 Control: when it is ended, saved, fails, or is left alone for 20 minutes, the heaters are switched off as soon as the printer is idle, unless a print started meanwhile or someone else set the bed target. See [the API details](../cc2-control/docs/API.md). The guided loop has not been validated on a printer yet.

## Quick Actions and global Emergency Stop

The dashboard now provides operational Quick Actions for Home All, All Heaters Off, Fans Off and Motors Off.

A global Emergency Stop remains accessible from every page and requires a deliberate one-second press-and-hold before execution.

## Idle motor release

The CC2 firmware's `idle_timeout` only switches the heaters off; unlike stock Klipper it never releases the steppers. After homing, calibration or jogging they stay energised, and the mainboard fan (`controller_fan board_cooling_fan`, which runs while any stepper is enabled) keeps spinning indefinitely although the part, auxiliary and chamber fans read 0 %. On V4.2-R5 the fan was still at 100 % (about 6 700 rpm) half an hour after a Bed Mesh calibration, and `M84` stopped it at once.

CC2 Control therefore sends `M84` once the printer has stood Idle for ten minutes with both heater targets at zero, no movement and no console command running, like Klipper's default `idle_timeout`. Printing, a paused print, any other machine state or stale telemetry restarts the wait, and the release is sent once per stationary period. Afterwards the axes must be homed again before manual moves.

## Settings and themes

Settings are organised into **Connection, Integrations, Appearance, About and Updates** tabs. The Updates tab checks a signed compatibility catalog and requires separate confirmation to download and install an application update; it does not update the printer firmware.

CC2 Control provides six themes (Dark, Light, Dracula, Nord, Monokai and Solarized Light). Saved appearance preferences are stored on the printer and shared between browsers, including the OrcaSlicer Device view.

## OrcaSlicer Device integration

For the step-by-step connection settings and filament-sync workflow, read [OrcaSlicer and Canvas sync](ORCASLICER.md) ([Italiano](ORCASLICER.it.md)).

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

The one-shot measurement requires the printer to be idle. The interactive session homes with `G28` at start and with `G28 Z` on each re-probe. Neither path runs `SAVE_CONFIG`.

## Bed Mesh 2D and 3D

The Bed Mesh view displays the printer's full 11×11 mesh with minimum, maximum, range, average, a 2D value map and an interactive 3D surface.

Screw-measurement results are captured separately and cannot replace valid mesh geometry.

## Nozzle thermal expansion

The printer homes and probes the bed with the nozzle at 140 °C, then heats it for printing. The nozzle grows as it heats, and the firmware's `[z_compensation]` raises Z by that growth on every move, from a table probed every 10 °C. The factory table ends at 250 °C. At and beyond either end of the table the firmware applies a constant (its slope × 140 °C, about 0.10 mm) instead of the measured growth, so materials printed above 250 °C get the wrong correction and need a different Z offset than the rest.

**Bed Levelling → Nozzle expansion** measures the table for the nozzle fitted now with the firmware's own `NOZZLE_TEMP_CALIBRATE`: the bed stays cold, the nozzle steps from the start to the end temperature, holds a minute at each step and probes the bed centre five times. The default 130–330 °C in 10 °C steps takes about an hour and keeps both the 140 °C reference and printing temperatures up to 320 °C inside the table; starts above 135 °C are refused for that reason. Unload the filament, clean the nozzle and use a clear plate first. The run is started with the LAN code, blocks other printer operations, and the header's Emergency stop interrupts it.

When the run ends the nozzle is switched off, and CC2 Control waits for the printer to confirm it; the page stays locked until then. The table is saved with SAVE_CONFIG and kept only when it is plausible (0.2–2 µm per °C, readings close to a straight line, no step downwards); otherwise the previous table is put back. A saved table applies after a printer restart, which the page offers. **Use factory table** removes it again, also from the next restart.

SAVE_CONFIG writes the settings the printer loaded when it started, plus its own changes, over `autosave.cfg`. So a run is refused while another saved change (a Canvas pull-back, a plate mount, a table) is still waiting for a restart, and after a run that was not kept the page asks for a restart: until then the printer still holds that measurement, and saving anything on its screen would write it back.
The chart compares the factory and saved tables relative to 140 °C.

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

`print_stats` includes the current file name, and `server.files.metadata` (WebSocket method, or `GET /server/files/metadata?filename=NAME`) returns the slicer's `filament_type` list and per-filament `filament_used_mm` for a file in the printer's internal G-code folder. Chamber heaters use them to choose a temperature for the material being printed. For example, DragonBreath firmware in **AUTO** looks the material up in its **Filament zones** table: set its control source to Klipper/Moonraker with the printer's address and port 7125. Only plain file names in the internal folder are read; USB files and paths return 404.

**Settings → Integrations → Panda Breath page** (off by default) adds a menu page that embeds the Panda Breath's own web page in a sandboxed frame. The address defaults to `pandabreath.local`, the stock firmware's mDNS name. The **DragonBreath firmware** switch renames the page to DragonBreath and changes the default address to `dragonbreath.local`; an address entered by hand (for example an IP address) is kept. CC2 Control does not contact the device: the browser loads the page directly, so it must be on the Panda Breath's network. All settings are stored in the browser. The contributor tested the embedded page with both stock BTT and DragonBreath firmware. Installation of this integrated CC2 Control build on a printer remains to be validated.

## Canvas, material presets and G-code library

CC2 Control supports native four-slot ELEGOO Canvas discovery and protected load, unload, material-editing and spool-mapping workflows.

User-defined material presets are stored under `/opt/usr/cc2-control`.

**Canvas → Canvas controls → Auto refill** shows and changes the Canvas setting that continues a print from another slot holding the same filament when a spool runs out (vendor method 2004). The switch appears only after the printer has reported the setting, and it changes only when the printer reports the new value.

**Canvas → Canvas controls → Pull-back after cut** sets how far the Canvas pulls a filament back after cutting it (`[canvas_dev] unload_fila_dist`, 60–350 mm). Raise it when the tip parks inside a shared tube or an aftermarket 4-in-1 adapter. The value is stored as an override in the printer's `autosave.cfg`, so the vendor configuration is not edited, and **Use printer default** removes it. The printer reads it when it starts, so after saving CC2 Control offers to restart the printer (idle only, about a minute and a half); otherwise it applies at the next restart. On a printer running the program with the firmware's limit raised to 1000 mm (issue #172), CC2 Control recognises it by its exact fingerprint and accepts up to 1000 mm, with a note on the page. Set 350 mm or less before the printer goes back to a stock program, which refuses to start with a larger value.

The G-code library lists printable files from internal memory and USB storage, including nested folders and thumbnails where available. USB jobs are validated and imported before the printer's native print-start workflow is used.

## Spools

**Spools** keeps a filament inventory and knows which spool sits in which Canvas slot or on the external spool holder. It is off until **Turn on spool tracking** is pressed on that page; the inventory can be edited either way.

- While printing, each spool is charged with the filament the printer actually extrudes from its slot, as the printer service reports it, including purges between colours and prints that are cancelled or fail. Slicer estimates are not used for the count.
- Inserting filament into a slot, or changing a slot's filament on the printer screen, opens **Which spool is it?** on every open page: a spool from the inventory, a new one with the filament the printer reports, or no spool. It suggests the spool that was there and the spools whose maker, material and colour match what is set for the slot on the printer; every other spool is one click away, grouped as in the inventory and searchable. Filament put back after even a few seconds asks again, as another spool can go in that fast. The question can wait: what the slot extrudes meanwhile is charged to the spool chosen later. After choosing a material and colour on the Canvas page, the same question follows. Choosing a spool whose filament differs from the slot's can also write the spool's maker, product line and colour to the slot.
- A spool that runs out while printing is set to zero once the printer moves on; a spool taken out of the printer goes back to storage with its remaining weight.
- **Weigh** corrects a spool: the scale reading minus its empty spool weight, or the filament left. Each spool keeps a history of prints, run-outs and corrections, and warns when it falls below its warning level.
- The inventory is grouped by manufacturer, then by kind (the material as entered, such as PLA Matte or PETG) and colour. Manufacturers with fewer than three spools share **Other**. Identical spools form one row with their count, fill levels, slots and total weight; opening the row lists them, opened spools first, emptiest first. A colour is marked as running low only when all its spools together fall below the warning level. **Add another like this** copies a spool as a new full one, and the slot question offers identical sealed spools once.
- The print dialog shows the spool each tool would draw from, its remaining weight and, when the file states its filament length, roughly how much the print needs.

The inventory is kept in `spools.json` under `/opt/usr/cc2-control`. The count is only as good as the remaining weight entered for each spool; weigh a spool to correct it. Moves made while a print is paused, such as a manual filament change, are not counted, nor is filament loaded outside a print.

## Print queue

**Queue** holds jobs prepared ahead: a G-code file, the choices of the print dialog (Canvas slots or the external feed, build plate side, bed calibration or saved mesh, timelapse, measurement and nozzle) and a number of copies. A job is added from the print dialog, which now offers **Start now** or **Add to queue**; the Files page marks files that still have copies queued.

- The page shows the next job with a pre-flight check (file in place, the filament in each mapped slot against the file, the side's saved mesh, the installed nozzle, printer idle and bed temperature) and one **Start print** button: no dialog, every choice was made when the job was added. While a copy prints, the card follows its progress and the start waits; a start the printer never picked up can be cancelled from the card while the printer is idle.
- Each copy is a separate print: the part is removed between copies. By default the next copy waits for the button, on the Queue page, in the Dashboard's **Next in queue** card or in the print-ended notice. **Start the next copy automatically** after a delay exists for a bed that clears itself and is off by default; right before such a start CC2 Control checks that every mapped Canvas slot still holds the file's material, and a slot that differs, is empty or cannot be checked leaves the copy waiting for the button, with a note.
- Copies are counted by CC2 Control on the printer, so the browser can be closed. A stopped or failed copy is not counted and the job stays first with a note. Jobs can be reordered, their copies changed, skipped or removed; finished jobs stay listed until cleared, or are removed automatically.

The queue is kept in `print-queue.json` under `/opt/usr/cc2-control`. The printer's own touchscreen takes no part in it: the vendor application cannot be extended, and its print-completed dialog gives CC2 Control no signal.

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

If the MJPEG stream freezes, follow the [read-only camera diagnostic guide](CAMERA_DIAGNOSTICS.md)
before restarting processes or changing printer/network settings. The documented
vendor `ai_camera` investigation is distinct from the CC2 Control viewer's ownership behavior.


Nozzle expansion is disabled by default. Enable its dedicated switch in Settings → Integrations → Experimental integrations before starting a run or restoring the factory table. Existing experiment choices migrate without opting into this feature; enabling its controls starts no heating or motion. Recovery and a pending table restart remain accessible when opted out.


### Nozzle temperature targets

On the supported Centauri Carbon 2 firmware family, manual nozzle targets and
preheat profiles accept 0–350 °C, matching the native touchscreen. This is a
command/input limit, not a change to the native heater configuration, sensor
limits or thermal shutdown protections. The hotend PID calibration retains its
separately qualified 150–300 °C range. A material's recommended temperatures are
not changed by this supported hardware range.

Manual homing restores a previously valid nozzle target up to 350 °C after the
native homing sequence. Merely opening the panel or saving/selecting a preheat
profile does not start heating; applying a target is a separate action.
