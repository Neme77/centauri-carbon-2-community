# CC2 Control Discovery API v1

Canonical endpoint:

    GET /api/v1/system/info

Compatibility alias:

    GET /api/system/capabilities

Purpose: allow local integrations (including future MoonFaker/CentauriPrime support) to detect the community firmware and available CC2 Control facilities without SSH probing or firmware-specific filesystem parsing.

The endpoint is read-only and does not expose the LAN access code, MQTT password, or other credentials.

Example response:

```json
{
  "platform": "cc2-community",
  "implementation": "Neme77/centauri-carbon-2-community",
  "device": "ELEGOO Centauri Carbon 2",
  "community_firmware": "4.2",
  "cc2_control": "1.1.31",
  "api_version": 1,
  "printer_uuid": "...",
  "services": {
    "cc2_control": 8081,
    "moonraker_compat": 7125
  },
  "capabilities": {
    "mqtt": true,
    "camera": true,
    "canvas": true,
    "file_manager": true,
    "gcode_thumbnails": true,
    "object_exclusion": true,
    "gcode_console": true,
    "orca_upload": true,
    "orca_print": true,
    "panda_compat": true,
    "material_presets": true,
    "persistent_ui_preferences": true,
    "print_history": true,
    "canvas_auto_refill": true,
    "canvas_unload_distance": true,
    "bed_plates": true
  },
  "endpoints": {
    "printer": "/api/printer",
    "health": "/api/health",
    "canvas": "/api/canvas",
    "files": "/api/gcode-files",
    "console": "/api/console",
    "preferences": "/api/preferences",
    "history": "/api/history",
    "plates": "/api/plates",
    "discovery": "/api/v1/system/info"
  },
  "runtime": {
    "mqtt_connected": true,
    "mqtt_registered": true
  }
}
```

## Compatibility contract

- `api_version` is the discovery schema version. New optional fields may be added within v1.
- Existing v1 field meanings should not change incompatibly.
- Breaking schema changes require a new `/api/vN/...` endpoint.
- `services` reports the actual ports selected at CC2 Control startup.
- `printer_uuid` may be empty until printer telemetry has supplied it.

## LAN access-code setup and rotation

- `GET /api/setup` reports credential and MQTT synchronization state.
- `POST /api/setup` accepts the initial LAN access code during first-run setup.
- `POST /api/setup/revalidate` atomically replaces an existing LAN code and restarts only CC2 Control.

## OrcaSlicer Canvas filament synchronization

- `GET /server/info` reports the live CC2 Control MQTT readiness state in Moonraker-compatible form.
- `GET /server/database/item?namespace=lane_data` exposes the cached Canvas trays as read-only AFC lanes for OrcaSlicer's Moonraker printer agent.

No additional polling, process, thread, or MQTT subscription is created by these endpoints.

## Panda compatibility bridge (port 7125)

- `GET /server/info`, `/printer/info`, `/printer/objects/list` and `/printer/objects/query`, plus the `/websocket` methods `printer.objects.query` and `printer.objects.subscribe`, report `webhooks`, `virtual_sdcard`, `print_stats` (state and file name), `toolhead`, `extruder` and `heater_bed`.
- `GET /server/files/metadata?filename=NAME` and the WebSocket method `server.files.metadata` return `filename`, `size`, `modified` and, when the slicer wrote them, `filament_type` and `filament_used_mm` for a file in the internal G-code folder. Each request reads the last 256 KiB of the file. Other names return 404.
- Everything else is refused: the bridge is read-only.

Metadata lookup rejects symlinks and special files. Invalid, non-finite or oversized filament-usage lists are omitted rather than returned as malformed JSON. An abbreviated filename is resolved only against a connected, recent active or paused print; exact internal file names remain readable outside a print. The tail read is bounded to 256 KiB per request, not a process-wide memory guarantee. The external heater remains responsible for its own temperature targets and safety controls.

## Printer replies to MQTT requests

Print starts without calibration (method 1020), Canvas auto refill (2004), the print history (1036) and
time-lapse rendering (1051) are published to the printer's MQTT API. HTTP `202 Accepted` means the request
was sent; the printer answers later with `result.error_code`. `GET /api/printer` reports the latest non-zero
code returned to CC2 Control's own requests, or `null`:

```json
"printer_error": {"sequence": 3, "method": 1020, "code": 1009, "age": 2}
```

`sequence` grows with every refusal and `age` is in seconds. Code meanings follow ELEGOO's elegoo-link SDK
(see `NOTICE.md`), for example 1009 printer busy, 1021 print file not found, 1026 bed levelling data missing.
`machine.sub_status` in the same response is the vendor sub-state; its meaning depends on `machine.status`.

## Files of 2 GiB or more

The printer program (elegoo_printer, 32-bit ARM) checks a print file with the 32-bit `stat()`; from
2 GiB on that fails and it reports the file as missing (read in its disassembly). On the printer a
2.5 GiB USB start was answered 202 after a 434.5 s copy and check, and the printer never began it
(measured). The operator decides: `POST /api/gcode-files/print` and `POST /api/queue/add` take such a
file only with the confirmation `large_ok` (ninth field of the print request; the queue request's last line, after its
filament lines); without it they answer 409 `{"accepted":false,"reason":"file_too_large_for_printer",…}`
(the queue: `{"ok":false,"reason":"file_too_large_for_printer",…}`) before anything is copied. A queued
job keeps the confirmation (`large_ok` in `GET /api/queue` and in `print-queue.json`; a job stored
before the field existed has none) and sends it with every start; the queue never starts such a file
without it.

After an accepted start of a confirmed file the 202 carries `"watch":<sequence>`, and CC2 Control
watches whether the printer begins it. `GET /api/printer` reports the latest watch as `print_watch`
(`null` before the first):

```json
"print_watch": {"sequence": 2, "state": "failed", "code": 1021}
```

`state` is `watching`, `started` (machine status 2 with print state `printing`, or a job uuid other than
the one before the start) or `failed` (the printer refused the start request, method 1020, with `code`;
or the printer is idle again 30 s after the start without a job, `code` 0). A failed start removes the
internal `CC2_USB_` copy it was given, where the file list's delete guard allows it and no queued job
names it; a queued copy goes back to `pending` with `last_result` `not_opened`. The print dialog stays
open until the watch is decided.

## Canvas auto refill

- `GET /api/canvas` adds `"auto_refill": true|false`, or `null` until the printer has reported the setting
  (full Canvas replies carry it; status deltas may not).
- `POST /api/canvas/auto-refill` with body `on` or `off` sends method 2004. It answers `409` while the printer
  has not reported the setting. After the printer accepts the change, CC2 Control requests the Canvas state
  again so the new value is read back from the printer.

## Canvas pull-back after the cut

`[canvas_dev] unload_fila_dist` is how far the Canvas pulls a cut filament back from the toolhead. The firmware
reads it at start-up from the printer configuration followed by `autosave.cfg`, where a later value wins.
CC2 Control stores its value as a `[canvas_dev]` override in the SAVE_CONFIG block of `autosave.cfg` and never
edits the vendor configuration. The firmware's SAVE_CONFIG keeps that section.

- `GET /api/canvas/unload` returns `{"editable":bool,"min":60,"max":350|1000,"extended":bool,"configured":mm|null,"override":mm|null,
  "pending_restart":bool,"restart":"none"|"restarting"|"printer_busy"|"launch_failed"|"reboot_failed"}`.
  `configured` comes from the printer configuration and `override` from `autosave.cfg`. `editable` is false
  when `autosave.cfg` has a layout CC2 Control does not edit (no SAVE_CONFIG marker, carriage returns, or a
  duplicate section or key). A failed restart is reported once.
- `extended` is true, and `max` is 1000, only while the running `elegoo_printer` is the V4.4 program with the
  `unload_fila_dist` limit raised to 1000 mm (SHA-256 `15ea2077…0ea1bc`, issue #172). CC2 Control hashes the
  running program once per printer process on a background thread; until that finishes, and for any other
  program, `max` is 350. A result belongs to one process (id and start time) and one file (device, inode,
  size, modification and change times); a file that changes while it is read, a process that has gone or been
  replaced by the time the result is stored, and two printer processes at once all give 350. A save above 350
  checks the running program again just before writing and answers `409` if it is no longer the raised one.
- `POST /api/canvas/unload` with body `60`–`max` (whole millimetres) sets the override, and with body `default`
  removes it. The firmware refuses to start with a value outside its range, so other values answer `400`. It
  answers `409` unless the printer is idle with fresh telemetry and nothing else in progress. The old file is
  kept as `autosave_backup.cfg`, as the firmware's own SAVE_CONFIG keeps it, and the reply is the new state.
  A change stays `pending_restart` until the system boots again.
- `POST /api/canvas/unload/restart` with body `RESTART` reboots the printer to apply a pending change. It is
  guarded like a build-plate mount: the reboot is cancelled if the printer is no longer idle when it would
  start.

## Nozzle thermal expansion

The firmware's `[z_compensation]` table (`nozzle_calibrate_start`, `nozzle_calibrate_diff`, `nozzle_calibrate_datas`,
relative to `fix_ref_temperature`) is read from the printer configuration; a measured table is stored as a
`[z_compensation]` section in the SAVE_CONFIG block of `autosave.cfg`, which the firmware reads after it.

- `GET /api/nozzle-expansion` returns `{"available":bool,"editable":bool,"state":"idle"|"running"|"cooling"|"saving"|"recovery",
  "result":""|"saved"|"rejected"|"failed"|"interrupted","detail":text,"start":C,"end":C,"step":C,"delay":s,"samples":n,
  "elapsed":s,"nozzle":C|null,"target":C|null,"reference":C,"factory":table|null,"override":table|null,"measured":table|null,
  "slope_um":µm/C,"residual_um":µm,"pending_restart":bool,"restart":"none"|"restarting"|...}`, where a table is
  `{"start":C,"diff":C,"values":[mm,...]}`. `available` is false without a `[z_compensation]` section; `editable` is
  false when `autosave.cfg` has a layout CC2 Control does not edit.
- `POST /api/nozzle-expansion/start` requires `X-CC2-LAN` and canonical text `start:end:step` (start 100 up to
  `fix_ref_temperature` − 5, or 200 without one, end up to 340, step 5 or 10, at least four and at most 48 points).
  It answers `409` unless the printer is idle with fresh telemetry, the printer's print state is standby, complete or
  cancelled, both heater targets are 0, the bed is at 40 °C or less, nothing else is running, and no saved change
  (pull-back, plate mount, nozzle table) is waiting for a restart: the run's SAVE_CONFIG writes the configuration the
  firmware loaded at start-up, plus its own changes, over `autosave.cfg` and would discard it.
- The run sends `NOZZLE_TEMP_CALIBRATE ... DELAY=60 SAMPLES=5` as one request with a budget of 15 min plus 8 min per
  point, then `M104 S0`, which may wait behind a calibration still running and counts only after its reply and a zero
  target readback, then `SAVE_CONFIG`, and checks the saved table: a slope of 0.2–2 µm/°C, every reading within
  15 µm of the fitted line and no step down of more than 6 µm. A table that fails is replaced by the section the run
  started with (the firmware's `autosave_backup.cfg` is left as SAVE_CONFIG wrote it). A printer-service restart
  during the run ends it. A session file (range and start time) makes a restarted CC2 Control switch the nozzle off
  the same way, unless a print owns the heater by then. Every end other than a kept table marks a required restart
  (`pending_restart`): the firmware still holds the measured table in memory and a later SAVE_CONFIG would write it
  back. Every other mutating request answers `409` while a run is active, except the emergency stop and console
  `M112`.
- `POST /api/nozzle-expansion/factory` with body `FACTORY` removes the saved section (idle only).
- `POST /api/nozzle-expansion/restart` with body `RESTART` reboots the printer to apply a saved table, guarded like a
  build-plate mount.

## Print history and time-lapse videos

- `POST /api/history/refresh` sends method 1036 (one request in flight at a time).
- `GET /api/history` returns what CC2 Control holds; it never contacts the printer:

  ```json
  {"available": true, "pending": false, "generating": false, "age": 4, "error_code": 0,
   "reply": {"id": 1036, "method": 1036, "result": {"error_code": 0, "history_task_list": [
     {"task_id": "...", "task_name": "part.gcode", "begin_time": 1790895600, "end_time": 1790897160,
      "task_status": 1, "time_lapse_video_status": 2, "time_lapse_video_url": "video/part.gcode20260101120000.mp4",
      "time_lapse_video_size": 4839487, "time_lapse_video_duration": 12}]}}}
  ```

  `reply` is the printer's latest successful reply, unchanged. `error_code` is the code of the latest reply
  (`-1` none yet, `-2` larger than the 256 KiB CC2 Control keeps). `task_status`: 1 completed, 2 and 3
  stopped, 4 printing, 5 paused. `time_lapse_video_status`: 0 not recorded, 1 frames not rendered yet,
  2 MP4 ready, 3 rendering failed.
- `POST /api/history/timelapse` with a `task_id` as body sends method 1051 to render that job's frames into
  an MP4. It requires status 1 or 3, an Idle printer with fresh telemetry, and no other rendering in progress
  (`generating`). The printer acknowledges immediately, then renders in machine state 12. CC2 Control
  requests the history again when it leaves that state. A bounded fallback handles missing state updates.
- `GET /api/history/timelapse?task=<task_id>` streams a ready MP4 (`video/mp4`, attachment). CC2 Control
  fetches it from the printer's own HTTP service on loopback with the LAN access code, which never reaches
  the browser, and shares the two-transfer limit of G-code downloads. That service sends `Content-Length`
  together with chunked framing and keeps the connection open after the response; CC2 Control relays the
  chunked framing and ends the transfer at the last chunk.

The printer lists its last 50 jobs (`result.total` was 50 on the tested printer) and ignores
`offset`/`limit` parameters.

## Live camera viewer

The web UI coordinates live camera viewing between CC2 Control pages. A new viewer takes over;
the previous page stops on its next existing printer-state poll. Brief overlap during handover is possible.

- `POST /api/camera/claim` with a text body of 8–40 characters from `0-9`, `a-z` and `-` (the page's random
  viewer id) makes that page the current viewer and returns `{"viewer":"<id>"}`. Like other commands it requires
  `X-CC2-Request: 1`; an invalid id is rejected with 400.
- `GET /api/printer` reports the current viewer as `camera_viewer` (`null` until a page claims the camera; the
  value is kept in memory only).

A page streaming live view stops when `camera_viewer` names another page, and offers to take the camera back. The
camera service itself is unchanged: other clients connected directly to port 8080 are not affected.

## A new print starts clean

`GET /api/printer` reports `print.current_layer`, `print.duration`, `print.total_duration`, `print.remaining` and `machine.progress` of the
running print only (for `print.remaining`, the estimate from the file stands in while the printer has not reported one, see `remaining_source`). When a new print begins they go back to 0 until the printer reports a new value, also when
it prints the same file as before (before, the layer of the previous print was still shown right after a start).
MQTT tells a new print by its job uuid (`print.uuid`) differing from the last non-empty one; the printer's direct
status stream (`print.native`, UDS) tells it by `print_stats.state` going from `standby`, `complete`, `cancelled` or
`error` to `printing`. A pause and resume, a reconnection in the middle of a print and the first value after the
service starts do not reset anything. The direct stream's filament use is dropped the same way.

## Layer path

`GET /api/gcode-files/layer-path?storage=internal&file=<name>&layer=<n>` returns the thinned extrusion path of one
layer. Answers: 200 with the path; 400 for a malformed request (`Invalid layer`, `Invalid protected G-code path`);
404 for a file that is not there or a layer outside it; 413 when the scan reached its time or size limit
(`Layer path scan limit reached`; after a scan from the start of the file the same or a higher layer of that file is refused at once for 300 s, unless a current layer map serves it, and a limit hit while reading through the map refuses only that layer for 300 s) or the
path is too large; 503 `Layer path busy; retry shortly` while another scan runs or rests after the last one (the
page retries it); 503 `Printer is homing or starting a print; retry shortly` during the read hold (see
[G-code reads while the printer homes](#g-code-reads-while-the-printer-homes)); 500 for a read error.

While a print runs, a request for **the file of that print** (`storage=internal` and the printer's file name) is
refused with **409 Conflict** before any G-code is read when it cannot belong to the print:

- `Layer is beyond the running print`: the layer is above the print's layer count (the count the printer reports,
  else the one found in the G-code file; no count known, no check).
- `The running print has not reached that layer`: the layer is above 1 and `print.current_layer` of
  `GET /api/printer` (the direct stream's value when it has one) is still below 1. A new print resets that value,
  so a layer left over from the print before (seen: layer 193, a 20 s scan of 78.7 MB right after a start) is not read.

A print counts as running while the printer is fresh (last message within 15 s), `machine.status` is 2 and the
state is `printing` or `paused`. Requests for any other file, for the USB storage, and every request while no
print runs (browsing by hand) are answered as before, apart from the read hold. The 409 is not retried by the
page, unlike the 503. The 409 refusals come before the read hold: a request they refuse is answered 409 as before.

`GET /api/health` counts this work, in memory only (the counters start at 0 with the service and hold no file
names, paths or other private data):

| Member | Meaning |
|---|---|
| `layer_path_scans` | Scans the worker ran, including those that ended in 404 or 413. An answer from the remembered layer, or a refusal before reading, is no scan. |
| `layer_path_rejected` | Requests refused with 409 as not fitting the running print. |
| `layer_path_busy` | 503 `Layer path busy` answers (another scan running, the pause after the last one, or the scan worker could not be started). |
| `layer_path_bytes_read` | Bytes the scans read from the files, counted from where each scan started (a remembered layer start, or the offset of the layer in its layer map, is skipped, not read). |
| `layer_path_longest_ms` | The longest scan, in milliseconds, from its start to the end of reading. |
| `layer_path_last_ms` | The last scan, in milliseconds. |
| `gcode_read_blocked` | Requests of the layer path, the object preview and `POST /api/gcode-files/thumbnail` answered 503 by the read hold before anything was read, and scans of the running print's file the hold kept from starting (once per withheld scan). |
| `gcode_read_stopped` | Layer-path and object-preview scans that the read hold ended while they read (counted in `layer_path_scans` as well), and scans of the running print's file it ended. |
| `layer_map_hits` | Layer scans answered through the file's layer map (see *Layer index storage*). |
| `layer_map_fallbacks` | Layer scans that read the file from its start: no current map yet, a map the index cannot serve, or a USB file. `layer_map_hits` + `layer_map_fallbacks` = the scans that read a file for one layer. |
| `layer_map_built` | Maps the background builder wrote since the start. |
| `layer_map_failed` | Map builds that failed (for example on full storage). |
| `layer_map_bad` | Requests that found a current map whose stored offset for the layer was no layer marker; they were answered by the full scan. |

The object preview below shares these counters: its scans count as `layer_path_scans` (with their bytes and
times), its busy answers as `layer_path_busy` and its start-protection refusals as `layer_path_rejected`.

## Layer index storage

At startup, CC2 Control accepts `--layer-index-dir <path>` to store per-file layer maps (built in the background while the printer is idle) outside the G-code folders. The default is a `layer-index` directory next to CC2 Control's preferences. A map is keyed to the G-code file by size, change time, inode and the content of its first and last 64 KiB, holds at most 8192 layers, and is removed when the G-code file is deleted through CC2 Control; the 64 newest maps are kept, older ones (for example of files removed on the printer's screen) are dropped after the next map is saved. A build that fails (for example on full storage) is not started again by a layer request for ten minutes; an upload, copy or import of the file tries at once. A minute after the start, at the first idle time, the 32 newest files of the internal storage are queued for their maps (the first open of a file would otherwise pay the full scan). The map file carries a CRC-32 of its contents and the sizes of its records; a damaged map, one of an older version or one written by other code counts as stale and is rebuilt. Before a layer is read through the map, the line at the stored offset must be a layer marker; if it is not, that request reads the file from the start (counted as a fallback and in `layer_map_bad`), and the first time for a file the map is deleted and the file queued for a new one (not again for the same file until the program restarts, the last 16 files being remembered, so a map that is built wrong again cannot cause a loop). A file of 2 GiB or more (see *Files of 2 GiB or more*) gets no map. A file without a current map is read with the full scan as before; the layer-path reply does not change.

## Object preview

`GET /api/gcode-files/object-preview?storage=internal&file=<name>&object=<name>` returns the coarse 3D shape of one
print object: the outer wall of a few of its layers. `storage` (`internal` or `usb`, default `internal`) and
`file` are checked like the layer path's; `object` is a name from `EXCLUDE_OBJECT_DEFINE NAME=…` (OrcaSlicer,
ElegooSlicer, PrusaSlicer). Example:

```json
{"storage":"internal","file":"part.gcode","object":"Box_id_0_copy_0","layers":89,"step":4,"z_max":25.6,
 "bounds":[22.63,106.4,64.78,148.55],"rings":[{"z":0.2,"paths":[[33.38,144.36,32.86,144.5,…]]},…],
 "truncated":false,"objects":["Box_id_0_copy_0","Lid_id_1_copy_0"]}
```

- The first request for a file reads it once, to the end, and collects every object; the result is kept in
  memory. Later requests for that file (same device, inode, size and modification time), for any object, are
  answered from memory without reading, also during the pause after a scan and during the read hold.
- An object's moves are those between `EXCLUDE_OBJECT_START NAME=…` and `EXCLUDE_OBJECT_END`; a `START` without
  a `DEFINE` adds the object. Only the outer wall counts: extrusions while the last `;TYPE:` comment is
  `Outer wall` or `Overhang wall` (OrcaSlicer, ElegooSlicer), `External perimeter` or `Overhang perimeter`
  (PrusaSlicer) or `WALL-OUTER` (Cura). A file without any `;TYPE:` comment counts every extrusion inside an
  object. Arcs (G2/G3), relative and absolute extrusion are read as by the layer path.
- `layers` is the number of layers in which the object has outer wall (n; layers are numbered by the layer
  marks, without them each new height is a layer). `step` is max(3, ceil(n / 24)), rounded up to a multiple of
  the step the scan had to keep (1, 2, 4 …: while reading at most 96 layers per object are kept, then every
  second one goes); `rings` holds the first object layer and every `step`-th after it, at most 24. For example
  n = 21, 65, 89, 128, 523 give steps 3, 3, 4, 6, 24 and 7, 22, 23, 22, 22 rings.
- A ring is its layer's height `z` and its outer-wall paths as flat `[x, y, x, y, …]` lists in bed millimetres.
  A ring holds at most 128 points over all its paths (at most 64 paths): points closer than 0.5 mm to the last
  kept one are left out, and the distance doubles while a ring would run over; the start and the end of each
  path stay. Points are stored in steps of 0.02 mm; numbers carry at most two decimals.
- `z_max` is the object's highest layer and `bounds` its `[xmin, ymin, xmax, ymax]` over all its outer-wall
  moves; both are `null` for an object without outer wall (`layers` 0, `rings` empty). `objects` lists every
  object of the file in the order they appear. `truncated` is `true` when the file defines more than 64 objects
  or a name longer than 127 bytes (those are left out), or when a ring had more paths than it holds or a point
  lay outside ±655 mm.

Errors, as `{"error":"…"}`:

| Status | Error | When |
|---|---|---|
| 400 | `Invalid protected G-code path` / `Invalid object` | Malformed request, unknown parameter, unsafe path, `object` missing or empty. |
| 404 | `G-code file unavailable` | The file is not there. |
| 404 | `No objects in the G-code file` | The file has no `EXCLUDE_OBJECT_DEFINE` or `EXCLUDE_OBJECT_START`. |
| 404 | `Object not found` | The file has objects, but not this one. |
| 409 | `Object preview waits for the print start` | While a print runs, a request for **that print's file** (`storage=internal`) before the service has seen layer 1 (the layer path's `The running print has not reached that layer` rule; layer numbers play no part here). Nothing is read. |
| 413 | `Object preview file too large` | The file is larger than 50 MiB (52428800 bytes); it is not read. |
| 413 | `Object preview scan limit reached` | The scan took longer than 20 s. For 300 s afterwards that file (unchanged) is refused at once, for any object. |
| 503 | `Object preview busy; retry shortly` | A layer-path or object-preview scan runs, or the pause after the last one lasts. |
| 503 | `Printer is homing or starting a print; retry shortly` | The read hold (see below): no read begins, and a read that runs when it comes on ends with this answer and is not kept. |
| 503 | `Out of memory` | Memory ran out; nothing is kept and the next request reads again. |
| 500 | `Cannot read the G-code file` | A read error. |

The scan runs on the layer path's single worker (a layer-path scan then answers `Layer path busy`, and the other
way round), at its lower priority and followed by its pause. One file is kept at a time; it is dropped, and its
memory freed, when another file is scanned, when the file changes, 30 minutes after the last request to this
route (checked on every pass of the main loop), when a print of another file starts (a new job uuid, or the
direct stream going from an idle state to `printing`) and when a print ends (`printing` or `paused` turning into `standby`,
`complete`, `cancelled` or `error`). What is kept for the file of the print that starts stays: the request for
it is still refused until layer 1 and is then answered from memory instead of a second read. A read of that file
that is still running when the print starts is ended by the read hold and read again after layer 1.
It holds at most 64 objects × 24 rings × 128 points.

## G-code reads while the printer homes

Seen on the printer on 12 and 18 November 2026 (#396): while the printer homed for a print start, a long read of a large
G-code file on the printer's two small cores came with the MCU shutdown `Timer too close` (error 803), and the
printer program crashed. So while the printer homes, levels, probes or starts a print, CC2 Control begins no read of
a G-code file for the layer path, the object preview or `POST /api/gcode-files/thumbnail` (the picture a slicer
embedded, read from up to the first 4 MiB of the file), whichever file is asked for. Those requests are answered
**503** `{"error":"Printer is homing or starting a print; retry shortly"}`. The hold is on while

- MQTT reports `machine.status` 5 (Auto leveling), 7 (Resonance test), 8 (Self check) or 10 (Manual homing);
- a print has not reached layer 1: `machine.status` 2, state `printing` or `paused`, and `print.current_layer` of
  `GET /api/printer` (the direct stream's value when it has one) below 1, which covers heating, homing, probing and
  the purge of the start;
- a console job of CC2 Control runs whose command homes or probes (a line starting with `G28`, `G29`, `G180`,
  `PROBE`, `PROBE_…`, `BED_MESH_CALIBRATE`, `SCREWS_TILT_CALCULATE`, `Z_TILT_ADJUST`, `SHAPER_CALIBRATE` or
  `TEST_RESONANCES`), for example the screw tramming session, because MQTT reports such a job a poll later;
- the bed mesh calibration of CC2 Control homes or probes (not while it heats or soaks).

The MQTT states count only while the printer is connected, registered and fresh (last message within 15 s, like
the other checks): without the printer nothing is held, so files can still be browsed, and a printer that stops
reporting releases the hold after 15 s. What is remembered is still answered (the layer path's last layer and its
scan-limit answer, the object preview's file and its size and limit answers); the 409 refusals for the running
print's file come first. A layer-path or object-preview scan that runs when the hold comes on ends at its next line
(the main loop passes the hold on at least once a second) with this 503; nothing of it is kept as a result. A
picture is read on the request thread and cannot be ended that way; its read keeps the service's priority, because
lowering it would also slow the main loop (MQTT, the emergency stop) and going back needs privileges. The page waits
and asks again every 3 s with the text "Waiting until the printer is ready…" (layer path, object preview); the
picture queue rests 3 s after such an answer and then asks for the same picture again. `gcode_read_blocked` and
`gcode_read_stopped` in `GET /api/health` count the answers.

The same hold applies to the scan of **the running print's file** that finds its layer count and print time for
`GET /api/printer` (`print.total_layers` when the printer reports none, the remaining time from the file). Seen on the
printer on 18 November 2026: that scan of a 52 MB file (about 21 s) ran into the homing of the print start, then
error 803; when it had ended 12 to 15 s earlier, there was none. While the hold is on, `GET /api/printer` (and the
layer path's 409 check) start no such scan and leave nothing pending; `total_layers` is then the count the printer
reports, or 0. The first call after the hold starts it. A scan that runs when the hold comes on ends at its next
check (every 64 KiB) without a result and is started again after the hold. The scan runs at the layer path's lower
priority on the analysis worker; because that worker also serves `inspect` and `metadata`, the priority is lowered
only when it can be set back (root, or `RLIMIT_NICE` allows it) and is set back after the scan.

## G-code file list and uploads

`GET /api/gcode-files` lists each storage (`internal`, `usb`) as
`{"available":…,"truncated":…,"count":…,"total":…,"files":[…]}`, newest first. A storage
lists at most 128 files: when it holds more, the newest 128 are listed, `truncated` is `true`
and `total` counts every G-code file found. The scan examines at most 4096 directory entries
per storage; beyond that `truncated` is `true` and `total` covers only the entries examined.

Deletion refusals include a stable `code` alongside the existing English `error` for API compatibility. The UI translates the code for single and bulk deletion and uses a localized generic refusal for unknown codes or malformed responses. Codes: `file_delete_fresh_status`, `file_delete_busy`, `file_delete_upload_busy`, `file_delete_pending_print`, `file_delete_active_print_unknown`, `file_delete_active_print`, `file_delete_state_unknown`, `file_delete_storage_unavailable`, `file_delete_unsafe_file`, `file_delete_failed`. HTTP statuses and safety decisions are unchanged.

`POST /api/gcode-files/delete` accepts deletions while the printer is busy if fresh
MQTT and native print state identify the job safely. The active, paused or errored
job and pending Orca starts are protected; in-flight uploads also block deletion.
Basename and case aliases are protected on
both media. Unknown/stale active-job evidence refuses deletion. Existing storage,
path traversal, symlink and hard-link restrictions remain; copying is still Idle-only.

A print started from USB runs from an internal `CC2_USB_<hash>_<name>` copy. After each accepted
USB start the oldest of these copies are removed so that the newest three remain, the one just
started always among them. Then, while all copies together exceed 1 GiB, the oldest of the rest goes
first; the one just started counts but is never removed, so it may stay alone above the limit with
the protected copies. A copy is removed only where `POST /api/gcode-files/delete` would
remove it (the same guard: never the active, paused or errored job, nothing while the printer
state is stale or unknown, an upload is running or an Orca start is pending) and only when no
queued job names it: as its own internal file, as the copy its start was given, or as the copy a
queued USB job imports to. Symbolic links, hard-linked files, `.part` files and other names are
never touched.

`POST /api/gcode-files/upload` never overwrites a file. When it rejects an upload after the
headers, for example with 409 because the name exists, it first reads the rest of the
request body, so a browser receives the error instead of a reset connection.

## Timelapse print option

`POST /api/gcode-files/print` accepts an optional sixth newline-separated field after storage,
filename, Canvas mapping, plate side and leveling mode: `1` enables timelapse for this print,
`0` disables it. Omission defaults to `0`; other values are rejected with 400.
The print popup exposes this selection for each job, initially unchecked.
Two more optional fields follow: a measurement of the mounted plate to print on (empty for the side's
saved mesh) and the installed nozzle (empty to keep it). A chosen nozzle is selected and its offset applied
before the start; a measurement that is neither the side slot nor stored in the printer, or a plate that is
not mounted on that side, is refused with 409. Calibrated starts ignore the measurement.
An optional ninth field `large_ok` confirms a file of 2 GiB (2147483648 bytes) or more; any other value is
refused with 400. See *Files of 2 GiB or more* below.

Saved-mesh starts include the flag as native method 1020 `config.delay_video`.
Calibrated starts first send method 1019 with `params.config.delay_video` and a unique request ID.
A matching successful acknowledgement is required within 1.5 seconds before the existing calibrated
G-code start runs; rejection, disconnect or timeout prevents the start. Both modes require printer validation.

## External feed print starts

A `POST /api/gcode-files/print` with an empty Canvas mapping (the external feed) first clears the
printer's Canvas colour table with one `CANVAS_SET_COLOR_TABLE T= CHANNEL=` request on the local
socket, as the touchscreen does. The print starts only after the printer acknowledges it. Otherwise
the reply is 503 with `"error":"Cannot clear the Canvas colour table for the external feed"`, and
nothing is started. Details are in `cc2-control/docs/GCODE-PROTOCOL.md`.

## USB print starts

Every `POST /api/gcode-files/print`, from internal memory as well as from USB, is checked on one start
worker: the request is answered at once only when it is refused without reading the file (invalid,
printer not idle, MQTT not ready); otherwise the leveling and Canvas tool check runs on the worker while
CC2 Control keeps answering other requests, and the main loop repeats every check and sends the start
afterwards. Only one start is prepared at a time (409 `Another print start is still being prepared`).
While a file's start is prepared, `POST /api/gcode-files/delete` refuses that name with
`file_delete_pending_print`. An internal file that changed during the check (size, inode or modification
time) is not started (409 `The file changed while the start was prepared`).

A `POST /api/gcode-files/print` from `usb` copies the file into internal memory as
`CC2_USB_<hash>_<name>` and prints that copy. The copy needs free space for itself, and once it
has replaced an earlier copy of the same name the 16 MiB storage reserve that uploads keep must
remain (the earlier copy counts towards the reserve, not towards the room the copy is written to);
when that room is missing, older `CC2_USB_` copies are removed right before the copy, once the checks
before it passed, oldest first and only those the cleanup after a start may remove, but only when that
gives enough room; otherwise nothing is removed and the start is refused before anything is written
(`507`, `Insufficient free space for the USB copy and storage reserve`). An upload into internal memory
(`POST /api/gcode-files/upload`, OrcaSlicer upload) makes room from old `CC2_USB_` copies the same way before it
begins. The USB file must be the
one that was scanned: if its size, inode or modification time differs, or fewer bytes were read
than it holds, the copy is discarded (`507`, `The USB file changed while it was copied`). After
`fsync` the copy is read back and compared with the bytes read from the stick (64-bit FNV-1a and
length); a mismatch discards it (`507`, `The internal copy of the USB file failed verification`).
Only a verified copy is renamed from its `.part` name, and only right before the start is sent, once the printer is confirmed idle: an earlier copy of the same name is never replaced under a print the touchscreen started meanwhile. An emergency stop or a client that leaves
stops the scan before the copy after the current line, the copy and its read back after the current
64 KiB block, and removes the `.part` file; an earlier copy with the same name is left as it was. A
`CC2_USB_` `.part` file left behind when CC2 Control ended during a copy is removed when it starts and before
every USB copy: a running copy holds a lock (`flock`) on its `.part` until it is renamed or removed, and the
lock ends with the process, so a `.part` without a lock has no copy behind it, whatever its age.
A start whose worker makes no progress for 15 s (a read the stick does not answer) is answered at once
with 409 `{"accepted":false,"reason":"usb_unresponsive","error":"The USB stick does not respond; remove
it, then start the print again"}`. The start stays busy until the worker returns from that read; then it
is cleaned up without a second answer. A stick that never answers again keeps further starts refused
until CC2 Control restarts. The 409 also carries `"stall":<sequence>`, and `GET /api/printer` reports the
latest such read as `usb_stall` (`null` before the first):

```json
"usb_stall": {"sequence": 1, "hanging": true, "outcome": null, "removed": true, "power_cycle": false}
```

`hanging` is true while the worker still stands in the read. `removed` says that the stick's mount
(`/mnt/exUDISK`) left `/proc/mounts` since; the table is read instead of the stick, as a `stat()` of a stick
that does not answer could hold CC2 Control. `power_cycle` is true when the read still hangs 60 s after the
stick was removed (untested and very unlikely: a stick that stops answering could not be reproduced on the
printer). Once the worker is back, `hanging` is false and `outcome` says how the read ended: `read_error`
(a read of the file came back with an error) or `slow` (it came back without one, only late). The worker
is never stopped by force. The print dialog shows these states and keeps its start button grey while the
read hangs.
The copy runs on its worker thread at normal priority and without pauses. Every 8 MiB it writes the
copy out and drops those pages of the source and the copy from the cache, so the copy does not pile up
unwritten data in memory; the read back drops the copy's pages the same way, every 8 MiB and at the end. A
USB start on a printer that is not linked or not idle is refused before the stick is read (503 `Printer
MQTT is not ready`, 409 `Starting a file requires an idle printer`), and so is a start from internal memory
on a linked printer that is not idle. Without a printer link a start from internal memory is checked as
before, at once and in the same order (request, file, then printer).
The page waits for the answer to a start at least 300 s, and for a file of known size 120 s plus 1 s
per 2 MB (500 MB: 370 s), at most 20 minutes; it knows the size from the file list and from the queue's
`size`. The rate is an assumption until the copy is measured on the printer. When the wait runs out the
page says so and closes the request, which cancels the start.
While a start reads a file, `POST /api/gcode-files/inspect` and `POST /api/gcode-files/metadata` for
that same file are refused at once with 409 `{"reason":"start_reading","error":"A print start is reading
this file right now; open it again once the start is answered"}` instead of reading it in parallel; a start
that finds an inspection of its file still running waits for it.
When the print dialog inspected the same file just before (`POST /api/gcode-files/inspect`, same
device, inode, size, modification and change time), the start takes its leveling and Canvas tool
decision from that cached inspection instead of reading the file again, so a refused start still
copies nothing. The copy then scans the bytes it copies with the same line logic and must reach the
same decision; if it does not, the copy is discarded (`507`, `The USB file changed while it was
copied`). Without such an entry (another client, a restart, another file inspected since) the file is
scanned before the copy, in one pass for leveling and tools when the start names Canvas trays.
When the copy starts, CC2 Control notes the Canvas trays; a queued start keeps exactly the trays of the fresh
report its slots were assigned from (see the queue below), and a queued USB start with a mapping whose slots
came without such a report is refused before the copy (409, `reason` `canvas_unreported`) instead of waiting
for a later report. After the copy it compares the trays the
start's slot mapping names (status, type, name and colour). If one of them changed meanwhile, the copy
is not printed (409 `The Canvas trays changed while the file was copied; start the print again`), and a
queued USB job is dropped as a refused start. For a USB file the trays after the copy come from a
report CC2 Control asks the printer for once the copy is done (method 2005); it waits up to 10 s for it
without holding up other requests, and an emergency stop or a client that leaves ends the wait. When no
such report comes in time, the start is refused (409 `The Canvas did not report its trays after the copy;
check that the Canvas is connected, then start the print again`). When the Canvas had not reported when a
start with a slot mapping began, CC2 Control asks for a report at once and keeps it as the trays at the start; the
file is checked meanwhile, but nothing is copied before the report. Without one within 10 s the start is refused
before the copy (409 `The Canvas did not report its trays when the start began; check that the Canvas is
connected, then start the print again`). Both Canvas refusals carry `"reason":"canvas_unreported"`, so the page
can keep the print dialog open to try again or change the mapping. Without a slot
mapping the trays are not compared and nothing is waited for.

## Delete print-history entries

`POST /api/history/delete` takes one task ID per line, at most 50 distinct safe IDs of up to 64
characters. It deletes only completed/stopped entries present in a history fetched within 60 seconds.
It requires connected, registered and fresh Idle state, and no pending deletion or video rendering.
The existing mutation header is required. Invalid IDs return 400; stale/busy/missing entries return 409.
An accepted request sends native method 1038 with `params.list` and returns 202 with the selected count.
A successful native reply triggers a history refresh; 202 alone is not confirmation of deletion.
`GET /api/history` also reports `deleting` while the bounded native reply wait is pending.

The UI asks for confirmation before deleting one entry or all completed entries in the loaded list.
The firmware handler inspected for this integration deletes database records; it does not remove
G-code files or timelapse files. This is a history operation, not a storage cleanup tool.

## Temperature targets during printing

`POST /api/control` accepts `heaters:set:<nozzle>:<bed>`, with nozzle 0–300 °C
and bed 0–120 °C. It requires connected, registered MQTT, a known idle or printing
machine state and a message received within 15 seconds. Finite values and full
request syntax are checked. The command sends two SET_HEATER_TEMPERATURE lines
without MOVE, PAUSE, M109 or M190; it does not wait for heating or alter print
state. A paused print is accepted while the native machine remains in printing
state. Other firmware operations remain blocked.

The Control temperature inputs and Apply targets button are available for idle,
printing and paused jobs. Initial fields require reported printer targets.
Preheat presets and calibration remain restricted to idle operation. A later
G-code heater command can replace the manually selected target, as on Klipper.
Manual temperature targets were validated during an active print on a physical CC2.

## Interactive screw tramming

`POST /api/control` accepts `screws:tram:start[:<C>[:2]]` (a trailing `:2` after an explicit target probes every corner twice:
`2× 3 probes`; the page shows one value per corner, the average of both results, the history keeps both), `screws:tram:probe:<FL|FR|RR|RL>[:2]`,
`screws:tram:check[:2]` and `screws:tram:end`. `<C>` is the bed target, a whole number of degrees from
0 to 110 (the touchscreen's bed limit) written with digits only; without it the target is 60 °C, and any other value is refused. Coordinates are taken from a fixed
corner table, the centres of the four bed screws (FL X27.9 Y25.95, FR X227.15 Y25.95, RR X227.15 Y225.5,
RL X27.9 Y225.5, laid out as one rectangle); unknown IDs are refused. Start homes all axes (`G28`) and uses the
same bed prep as `screws:measure` (right after a bed mesh it homes only Z, see "Directly after a bed mesh" below). Re-probe and the four-corner check home Z only
(`G28 Z`), and only while a session is active with X and Y homed. Every session
measurement, the start included, then waits 30 s (`G4 P30000`; the firmware's `G4`
ignores `S`) before its first probe. Each start/re-probe/check script restores `CC2_SCREW_TRAM` gcode state,
switches the nozzle off (`M104 S0`) and keeps the bed at the session's target (`M140 S<C>`). `GET /api/printer`
reports `screws_session.active`, `screws_session.xy_homed` and `screws_session.bed` (the bed target in
whole °C the running session measures at, `null` without a session). While the session's own
job runs, and for 15 s after it completed, the printer may report Manual homing or
Auto leveling instead of Idle; the session accepts those two states only then. The
session is cleared on disconnect, console error, timeout (10 minutes without a session action), emergency stop, print start,
lost X/Y homing, `heaters:off`/`system:heaters_off`, a bed mesh calibration started between two measurements, any other
machine state or end. A running session is never replaced: `screws:tram:start` is refused with 409 while a session is
active (`A screw session is already running: end it first`), and so is every `screws:measure` form (`A screw session is
running: measure and re-probe there, or end it first`; the one-corner form says `re-probe the corner there`).
The firmware switches an idle bed off 15 minutes
after the last command, so between jobs the session sends `M140 S<C>` again every ten
minutes (retried after 30 s if not delivered), only while the printer is idle and
telemetry still shows the session's target. The restart marker holds that target, so a bed a
killed process left at it is switched off by the next one; an empty marker from an earlier
version stands for 60 °C. End switches both heaters off. After any other
invalidation `TURN_OFF_HEATERS` stays pending and is retried until it is delivered
while the printer is idle and the console free; a print start or a queued heater
action (temperature, preset, preheat, PID, heaters off, a new tram start, a hold time) cancels it.
A bed hotter than the target by more than 2.5 K (the printer's own report, for a target of 5 °C or more) is cooled first,
before the nozzle is cleaned (`G180 S3`): the script waits with `M190 S<C-2.5>` and then sets `M140 S<C>`. A bed up to
2.5 K above the target starts as it is. The printer's `M190`
releases once the bed is within 5 K of its target and no longer changing fast (the firmware's PID settle window), so a
plain `M190 S<C>` would release a cooling bed up to 5 K early, and the wait above releases it at `<C>` + 2.5 K; after
such a wait the quick measurements hold 15 s (`G4 P15000`) at the target. This
applies to the session start and to every quick measurement. The session's idle limit (10 minutes) counts only while no
job runs and starts again when a job ends. The one-shot `screws:measure[:<C>]` heats and waits for the bed at `<C>` (60 °C without it) and
switches both heaters off afterwards. A trailing `:2` after an explicit target (`screws:measure:<C>:2`,
`screws:measure:corner:<FL|FR|RR|RL>:<C>:2`) probes every point of the measurement twice (`2× 3 probes`,
as `check:2` and `probe:<ID>:2`); a lone `:2` is the target 2 °C. The one-corner form
`screws:measure:corner:<ID>[:<C>[:2]]` measures front left (the reference) and that corner; when X and Y
are still homed it homes only Z (`G28 Z`), waits for the bed (`M190`) and for 30 s (`G4 P30000`) and does not
run the nozzle cleaning (`G180 S3`) again, otherwise it prepares as the four-corner measurement does.

The nozzle reaches its probing temperature through the measurement's own preparation: `G180 S3` (the firmware's
cleaning, which homes and ends at 140 °C), `G28` and `G28 Z` (the firmware's homing heats the nozzle to 140 °C).
While the printer stands idle the firmware switches the nozzle off after 300 s and the bed 600 s later; the measurements
heat both again themselves.

**Hold time.** `screws:soak:<M>:<measurement>` holds the bed at the measurement's target for `<M>` minutes before
`<measurement>` starts. `<M>` is 0 to 60, one or two digits (the soak limit of `/api/plates/calibrate`);
`<measurement>` is `screws:measure[:<C>[:2]]`, `screws:measure:corner:<ID>[:<C>[:2]]` or `screws:tram:start[:<C>[:2]]`.
With `0` it runs exactly as without the prefix: the same script and the same answer. Other minutes answer 400
(`Screw hold time must be a whole number of minutes from 0 to 60, followed by the measurement`), and so does any other
action after the prefix, the session's `screws:tram:probe` and `screws:tram:check` included (`A hold time is only
available for screws:measure and screws:tram:start`); a measurement longer than 95 characters after the prefix answers
400 (`Invalid action`). Every check of the measurement runs when the hold time is accepted, with the same 409 as without
it (printing, not idle, a running session, a malformed target, a busy console, and the refusals of every printer change
while maintenance, an RFID lifecycle or scan, a firmware calibration tool, the load-cell session, an application update
or network change, bed heating or the nozzle expansion calibration runs), and again when the hold time ends. After an
emergency stop, until CC2 Control restarts, a hold time answers 409 (`An emergency stop was sent: restart the printer
before a hold time`) and nothing is sent. CC2 Control then sets the bed target through the console (`M140 S<C>`; the answer is
`{"accepted":true}`), waits until telemetry reads the bed at the target (at most 1 K below it and, for a target of 5 °C
or more, at most 2.5 K above it, so a hotter bed cools first), and counts the hold time down with the console free (no
`G4`). It sends `M140 S<C>` again every ten minutes while fresh MQTT reports Idle and fresh telemetry shows its target
(the firmware's idle bed shutdown; a failed send is retried after 30 s). At the end it waits for a console command typed
meanwhile, then up to a minute for MQTT Idle within 15 s and the bed at its target, runs the checks again and starts the
measurement with its unchanged script, cancellable as usual. It never touches the nozzle. The job needs no open page;
a restart of CC2 Control ends it, and the bed then stays at its target until the firmware switches it off 15 minutes
after the last command, as after a calibration that a restart ended.

`screws:cancel` ends the hold time at once (`202`). A print start, an emergency stop, `heaters:off`,
`system:heaters_off`, `system:all_off`, another accepted screw action (`screws:tram:end`), a bed mesh calibration,
another machine state than Idle or a bed target set elsewhere end it as well. The bed is switched off (`M140 S0`) when
the job is cancelled or fails itself (the bed does not reach the target within 30 minutes, its `M140` fails, the evidence
at the end stays missing for a minute, the checks at the start refuse the measurement, the console stays busy for a
minute after the end), and only while fresh MQTT reports Idle and fresh telemetry shows the job's target; a job cancelled
before its own `M140` reached telemetry switches the bed off once telemetry shows it, within 15 s. While it runs, every
`screws:measure…` and `screws:tram:start…`, with or without a hold time, answers 409 (`A screw measurement is waiting for
its bed hold time: cancel it first`), and so does `POST /api/plates/calibrate` (`A screw measurement is waiting for its
bed hold time`). The starts of those sessions answer the same 409 (`A screw measurement is waiting for its bed hold
time: cancel it first`): `POST /api/nozzle-expansion/start`, `/api/bed-drying/start`, `/api/bed-drying/preheat`,
`/api/calibration-tools/action`, `/api/app-update/upload`, `/api/app-update/download`, `/api/app-update/install`,
`/api/network/action`, `/api/network/scan`, `/api/rfid/action` and `/api/rfid/lifecycle/action`. A session that
CC2 Control does not start itself (maintenance by the installer) is found by the checks when the hold time ends. A
session start that the console refuses at the end is tried again without starting a session, so no `end_reason` is
recorded for it. `GET /api/printer` reports `screws_soak`: while it runs
`{"active":true,"stage":"heating"|"soaking","bed":<C>,"remaining":<s>,"total":<s>,"action":"quick"|"corner"|"session"}`
(`remaining` equals `total` while the bed heats), otherwise `{"active":false,"result":<r>,"reason":<text>}` for the last
job since the start of CC2 Control: `result` `""` (none yet), `started`, `cancelled`, `ended` (stopped by one of the
events above) or `failed`; `reason` says why it ended or failed (for example `A print started`, `The bed target was
changed` or the 409 text of the check that refused the start) and is empty otherwise.

**Directly after a bed mesh.** A four-corner `screws:measure[:<C>[:2]]` or a `screws:tram:start[:<C>[:2]]` that follows
a bed mesh calibration of CC2 Control directly leaves out the nozzle cleaning (`G180 S3`) and, for the session, the full
`G28`. It prepares as the one-corner form with homed axes does: `M140 S<C>`, `M190 S<C>`, `G28 Z`, `G4 P30000`, then the
corners (with a hotter bed the cooling wait comes first, as everywhere). Directly means: the calibration's probing
succeeded (`result` `done`, `saved` or `work`), it ended at most 180 s ago, no other console job has run since, X, Y and
Z are homed and the measurement's bed target is the calibration's temperature. Otherwise the measurement prepares as
described above. A hold time given with it is left out, because the bed was just held for the mesh. The answer is then
`{"accepted":true,"after_mesh":true}`, and `GET /api/printer` reports `screws_after_mesh` `true` while that measurement
is queued or runs (`false` otherwise). The one-corner form keeps its own short way. While a bed mesh calibration runs
(any stage until it has ended), every `screws:measure…` and `screws:tram:start…` answers 409 (`A bed mesh calibration is
running`).

CC2 Control does not clear the X/Y homing a session depends on: `system:motors_off` is refused while a
session is active (409, `Motors off is blocked during a screw tramming session; end the session first`),
and the automatic stepper release after 10 idle minutes with both heater targets at 0 (`M84`) waits
until the session has ended and then starts its full wait again.

### Screw tramming history

Every successfully completed session job (start, re-probe, check) is recorded with
the raw `Result is z=` value of each probed corner, taken from that job's console
output. A completed one-shot `screws:measure` is recorded as its own closed entry with
`"kind":"quick"`: it has no session, and the 30 s settle only where it homes Z alone (one corner with X and Y
still homed, or directly after a bed mesh), so its values are kept apart from session values. The newest 20 sessions (64 measurements each) and, separately, the newest
10 quick entries are kept in `screw-history.txt` next to the UI preferences
(`--screws-history FILE` overrides it), written atomically after each change, so they
survive a reload, the end of the session and a restart. A session left open by a restart
is closed at its last measurement. A failed job and a re-probe without a result for its
corner are not recorded.

`GET /api/screws/history` returns the newest entry first and its measurements
oldest first. Each entry has `kind` `session` or `quick`, and `end_reason`: why a closed session
ended — `user` (end), `emergency_stop`, `disconnected`, `print_started`, `telemetry_stale` (no
printer report for 15 s), `console_error` (a job failed or could not be queued), `machine_state`
(another or no machine state), `timeout` (10 minutes without an action), `xy_homing_lost` (X/Y homing
cleared outside CC2 Control, for example on the touchscreen, by another client or by the firmware),
`quick_measurement`, `heaters_off`, `new_session`, `mesh_calibration` (a bed mesh calibration was started between two
measurements of the session) or `restart` (CC2 Control restarted with the
session open) — and `null` while it is open, for quick entries and for sessions recorded before
the reason was stored. It is an optional fourth field of the session line in `screw-history.txt` (`-` when there is no reason but a fifth
field follows); the fifth field `c` marks a session probed at the screw centres. `points` is `screws` for such a session
and `old` for one recorded at the earlier points (30/30, 230/30, 230/225, 30/225), whose heights are not comparable with
the new ones. Times are Unix seconds (UTC);
`ended` is `null` while a session runs; measurement `kind` is `start`, `probe`, `check`
or (in a quick entry) `quick`; `bed` is the bed target in °C the job measured at (absent for
entries recorded before it was stored); `ist` holds the actual temperatures in °C at the last probe of the job (`bed`,
`nozzle`, `chamber`, only the sensors that gave a value; absent for earlier entries); `corner` is set for `probe` and for
the one-corner quick measurement; `z` holds the corners the job measured (a re-probe
only its own corner), in mm; `z2` is present only for `twice` and holds the second
result of each corner; `probes` (after `bed` and `ist`) holds the single probes (`probe at X,Y is z=…`) of the corners that have them,
up to six per corner (2× 3), kept only when the average of a `PROBE`'s probes is its `Result` within 1 µm. Offsets against the
front-left screw are computed by the client. A cancelled job and a job that failed are not recorded.

```json
{"sessions":[
  {"kind":"quick","started":1762801500,"ended":1762801680,"measurements":[
    {"time":1762801680,"kind":"quick","corner":null,"twice":false,
     "z":{"FL":-0.682100,"FR":-0.676000,"RR":-0.870400,"RL":-0.751000},"bed":60,
     "ist":{"bed":60.4,"nozzle":28.1}}],"end_reason":null,"points":"screws"},
  {"kind":"session","started":1762800000,"ended":1762801200,"measurements":[
    {"time":1762800300,"kind":"start","corner":null,"twice":false,
     "z":{"FL":-0.681800,"FR":-0.675800,"RR":-0.869800,"RL":-0.750800},"bed":60},
    {"time":1762800420,"kind":"probe","corner":"RR","twice":true,
     "z":{"RR":-0.700100},"z2":{"RR":-0.699400},"bed":60}],"end_reason":"user","points":"screws"}]}
```

`POST /api/screws/history/clear` deletes the whole history and returns `{"ok":true}`
(`500` with `{"ok":false,"error":...}` when the file cannot be written). Both routes use
the same authentication and browser guard as the other `/api` routes.

## Bed alignment with a mesh

With "With mesh" ticked, the Bed alignment tab evaluates the four screw points with the plate shape of a saved mesh of
the plate library, of the temporary mesh or of the printer's active mesh (`GET /api/mesh`, read when the tab opens and
whenever the printer's phase changes, not during a screw session), entirely in the page; it needs no endpoint of its own.
After a screw measurement the active mesh is empty (`profile_name` "", `probed_matrix` [], `mesh_min`/`mesh_max` 0, measured on a
CC2: the firmware's nozzle cleaning unloads it) while `profiles` still hold the stored meshes; the tab therefore evaluates
with a copy taken in the browser when the measurement starts. "Measure a new mesh"
starts `POST /api/plates/calibrate` with the plate chosen in its question, the hold time of step 1 as the soak and the
line `work`, so the mesh becomes only the plate's temporary mesh (`calibration.work_mesh`, see below) and the library
stays as it was. Once it is measured, the page itself starts the way of measuring chosen in step 1 through
`/api/control`, without a hold time: the four-corner measurement (`screws:measure[:<C>[:2]]`) or the start of a screw
session (`screws:tram:start[:<C>[:2]]`), which then follows the mesh directly (see "Directly after a bed mesh" above).
That follow-up runs in the frame of the page, on every page of CC2 Control and after a reload, but it needs the
browser tab to stay open; while the printer answers busy it tries again every 5 s for up to 5 minutes, and otherwise
it leaves a note with a start button. After turning, only the screws are measured again, evaluated with the temporary
mesh, and the final mesh (a plain calibration with the plate, which replaces its stored measurement and ends an open
session) is measured only when the user asks for it, which the page offers once every corner of the latest
measurement is within the tolerance (±20 µm). Until then the page says that the mesh is temporary; once a later screw
measurement differs from the first four-corner measurement after the temporary mesh (the offset of a corner
to front left changed by at least 25 µm, however large it was before; without turning it changes by about ±6 µm, and
by up to about 20 µm while the bed soaks), it warns that the stored mesh no longer
fits the bed. The page remembers the temporary mesh it saw in the browser, so that warning stays after a restart of
CC2 Control. The guided loop of the earlier version, a session with a full mesh per round,
is removed: `/api/screws/align`, `/api/screws/align/measure`, `/api/screws/align/save` and `/api/screws/align/end` no
longer exist, and the endpoint list of `GET /api/v1/system/info` no longer has `screws_align`.

A running screw measurement (`screws:measure`, `screws:measure:corner:<ID>`, the `screws:tram:` jobs except the end)
is sent to the printer line by line and can be cancelled: `POST /api/control` with `screws:cancel` answers `202`
while one runs or a hold time counts (which then ends, see "Hold time" above) and `409` otherwise. The job stops after the line that is running (a line that waits, such as the bed
wait or the nozzle cleaning, ends first), lifts the nozzle (`G1 Z10`) only when X, Y and Z are homed (the printer
reports them when the job starts, and the job's own `G28` and `G180 S3` count once completed; without them, or after an error report, the lift is
left out, because the printer service accepted it with unhomed axes), restores the saved gcode state and sets
`M104 S0`. A quick measurement and the start of a session then switch the bed off as well (`M140 S0`) and a cancelled
session start ends the session (`end_reason` `user`); a later measurement of a session sets the bed to the session's
target again and keeps the session. A cancelled job is not recorded in the history. `GET /api/console` reports
`cancellable` (a cancellable job runs), `cancelling` (a cancel is requested) and `cancelled` (the last job ended by a
cancel). Requires printer validation.

## Console output

`GET /api/console` returns the state of the last console command of CC2 Control:
`busy`, `completed`, `success`, `generation`, `command` and `output` (up to 256 KiB, JSON-escaped).
`generation` rises with every command and every clear. Within one generation the stored output only grows.

`GET /api/console?since=<generation>:<output_len>:<instance>` lets a client that polls leave out the output it
already holds. The `generation` and the `output_len` of the reply it holds are plain decimal digits; `instance`
is the 16 lowercase hex digits that the same reply named (`instance`), one value for every start of CC2 Control:
the generation counter and the stored output start again with the service, so a restarted service can reach the
same two numbers with other text, and only the same instance vouches for the text. When no command runs, the
instance and the generation are the same and the stored output is exactly `output_len` bytes long, the reply
has no `output` and carries `"unchanged":true`; every other field is the same as in a full reply. A reply to a
request with a valid `since` carries `output_len` (bytes of the stored output, not of the JSON text, which leaves
out control characters) and `instance` in both forms. While a command runs, after a new command or a clear,
whenever the length differs and whenever the instance is missing or another one (the older form
`since=<generation>:<output_len>` and a service that started again), the reply is complete, so a client always
sees growing output. A missing or malformed `since` (not `<digits>:<digits>` or `<digits>:<digits>:<16 hex digits>`,
a sign, an overflow, an empty value) is ignored: the reply is identical to a request without it and never an
error. Older clients that send no `since` are unaffected. A client that stores the last `output` together with
its `output_len` and `instance` can show the stored text again whenever `unchanged` is true and the `instance`
is the one it sent; the Console page and the screw view of the web interface do. An older CC2 Control ignores
the parameter and always replies in full (without `instance`), so a client must not rely on `unchanged`.

## Heater PID calibration

`GET /api/pid` returns the selected heater, busy/ready/failed flags and the last
reported Kp/Ki/Kd values. It reads the existing console output rather than querying
the printer. The Control panel polls this cached state only while visible.

`POST /api/control` accepts `pid:extruder:<temperature>` (150–300 °C),
`pid:heater_bed:<temperature>` (40–120 °C) and `pid:save`, with the usual browser
mutation marker and connected/fresh/idle controls. Calibration runs
`PID_CALIBRATE` followed by `TURN_OFF_HEATERS`. Saving requires a successful
console calibration with the native `pid_calibrate: completed` report; it sends
`SAVE_CONFIG`, persisting the values. The UI
requires confirmation for calibration and saving.

Hotend calibration and PID persistence were validated on a physical CC2.
No restart was observed after saving. Immediate application of the new PID
coefficients has not been independently verified.

## X/Y motor mode (StealthChop)

Experimental: disabled by default and explicitly opted into in Settings → Updates,
independently of the stable application update channel. Quiet commands are refused
until opted in; Normal remains available for recovery. Not yet physically qualified
on V4.4 / V4.4.1. Passing CI is not printer qualification.

`POST /api/control` accepts `motors:quiet` and `motors:normal` while the printer is
idle with fresh telemetry; both are refused during a print. They change the X and Y
TMC2209 drivers together (CoreXY) with the native `SET_TMC_FIELD`, which refuses a
literal `VALUE=0`, so the zero writes use values whose masked field is 0
(`en_spreadcycle` 2, `tpwmthrs` 1048576).

StealthChop loses steps at high speed and acceleration, and a hybrid `tpwmthrs`
threshold does not cap the speed. Quiet therefore runs `M400` and
`SET_VELOCITY_LIMIT VELOCITY=80 ACCEL=2000` before any StealthChop write; Normal
restores SpreadCycle on both steppers first and then
`SET_VELOCITY_LIMIT VELOCITY=500 ACCEL=20000`, the stock `printer.cfg` limits
(CC2 Control does not read the printer configuration). Normal always restores these
fixed values, not limits a user set before Quiet. Both end with `DUMP_TMC` for X and
Y, shown in the Console.

`GET /api/printer` reports the last requested mode as `motor_mode` (`"quiet"`,
`"normal"`, or `null` before any request and after the printer service reconnects,
when the configured mode returns). It records the last accepted request, not a
verified TMC register state: check the `DUMP_TMC` read-back. It does not report the
toolhead velocity or acceleration limits.

Quiet belongs to one print. The 80 mm/s / 2000 mm/s² values are initial limits,
not an enforced cap: a slicer's `M204` or `SET_VELOCITY_LIMIT` lines raise them
again during the print, so Quiet suits only G-code sliced to stay at or below
80 mm/s and 2000 mm/s² throughout; the Control page warns while a print runs in Quiet. When a
print (printing or paused) has run in Quiet and the printer is idle again, CC2
Control sends the Normal script itself.

## Build-plate library

CC2 Control keeps named plate surfaces in `bed-plates.json` (`--plates FILE`, next to the UI preferences):
each one has a side, a Z offset of up to ±1 mm and the 11 × 11 meshes the printer measured on it, one per
bed temperature (30–110 °C) and nozzle, 20 in all. The file also lists up to 8 nozzles, each with a diameter
and a Z correction of up to ±0.5 mm, and which one is installed. A version 1 file (one mesh per plate) is
rewritten once as version 2 with each mesh as a 60 °C measurement, the temperature the firmware calibrates at.
`POST /api/plates/create` accepts `side\nname\nz_offset` and registers a profile without
reading or changing the printer mesh. It requires fresh idle telemetry and an unlocked library.
Such profiles appear in the normal `/api/plates` list with `measure:""` and `measures:[]`,
can receive the first calibration, and cannot be mounted before a measurement exists.
The version-2 file stores them in a separate optional `uncalibrated` array so older
apps can still read the calibrated plates. An older app may discard unfinished profiles on save.
Calibration form selections survive page/tab navigation in the browser; while a job
runs, its server-reported side, temperature, soak and destination are authoritative.
Measurement timestamps are Unix seconds; the UI displays date, time, seconds and local timezone.

Printer facts this relies on, read from the V4.2 firmware and its logs:

- Every print loads the mesh of its side: Side A uses profile `default`, Side B `default1` (`G180 S7`).
  A `BED_MESH_PROFILE LOAD` sent before a print is therefore replaced; a plate has to be in that slot.
- `BED_MESH_PROFILE SAVE=default` is refused by the firmware, and the firmware's own `RESTART` left its
  printer service hung until a reboot in a test on the user's printer. A slot is therefore written by
  replacing that one `[bed_mesh …]` section of `/opt/usr/cfg/autosave.cfg` (`--autosave FILE`) in the
  firmware's own format and rebooting the printer, which reads the file at start.
- The stock print start adds a per-side offset with `SET_GCODE_OFFSET BED_ROUGHNESS=…` (A −0.06, B −0.015) and
  keeps the value last set with `SET_GCODE_OFFSET Z=…`. A restart clears it. The plate Z offset is that value.
- The touchscreen's Z offset setting keeps its own value, 0 when the screen program starts, and sends it with
  `SET_GCODE_OFFSET Z=… MOVE=1`; it never reads the printer's offset, so a press there replaces the plate value.
- `BED_MESH_CALIBRATE … BED_TEMP=t` heats the bed to `t` (`M140`/`M190`) before probing.
- `BED_MESH_PROFILE SAVE=<name>` stores the active mesh under any other name and writes `autosave.cfg` at
  once, without a restart, and `BED_MESH_PROFILE LOAD=<name>` makes a stored profile the active mesh.
  The firmware runs a command received over its socket between the lines of a running print file.
- The applied Z offset is the plate's plus the installed nozzle's correction. With load-cell probing the
  nozzle is the probe, so the bed shape does not depend on it and every nozzle uses the same mesh.

Each measurement is also kept in the printer as its own profile, `cc2_<measurement id>`: when it is
created (the slot holds it, so CC2 Control sends `BED_MESH_PROFILE LOAD=<slot>` and `SAVE=cc2_<id>` and reads
the file back), when its plate is mounted in place, and before a calibration or a restart replaces the slot
that holds it. A print started from CC2 Control on the saved mesh may choose a measurement of the mounted
plate. When it is not the side's slot, CC2 Control watches the print start through `bed_mesh.profile_name`
in its telemetry subscription: whenever the printer has loaded the side slot (before the file and again at
`G180 S7`) and the first layer has not begun, it sends `BED_MESH_PROFILE LOAD=cc2_<id>` (at most eight
times per print). A load needs positive proof of that window: fresh telemetry reading layer 0 of this
print (`print_stats.info.current_layer`, which a new print resets) while it is printing. A stale count from
the last print, a stream that reconnects after the first layer began, or a pause therefore never load. A
load that comes too late leaves the side mesh in use and is reported as `missed`; an adaptive mesh
(`ADAPTIVE`) is never replaced. Requires printer validation.

Endpoints (all changes are `POST` with a text body, one field per line, and need `X-CC2-Request: 1`):

- `GET /api/mesh` returns the printer's active mesh (`profile_name`, `mesh_min`, `mesh_max` and the measured
  `probed_matrix`; not the interpolated `mesh_matrix`) and, under `profiles`, the side slots `default`, `default1` and `ADAPTIVE` read from
  `autosave.cfg`. It never asks the printer for `{"bed_mesh":null}`: the firmware serialises every stored
  profile on its motion thread, which during a print can end in "MCU 'mcu' shutdown: Timer too close".
- `GET /api/plates` returns the library, which plate is mounted, whether its Z offset has been applied
  (`z_applied`) and the value applied with the installed nozzle (`z_effective`), a mount waiting for the
  printer restart, the last mount result (`mounted`, `verify_failed`, `reboot_failed`), the printer's active
  mesh (`mesh_profile`), the measurement watched for the latest print (`print_mesh`: `state` `waiting`,
  `active` or `off`, `result` `loaded`, `missed`, `adaptive`, `failed`, `not_started` or `ended`) and the
  nozzles. Per plate it tells whether the measurement it mounts with (`measure`) is the one now saved for
  its side (`in_printer`); per measurement its temperature, nozzle, date, mesh and whether it is the side
  slot (`slot`) or stored in the printer as its own profile (`profile`).
- `/api/plates/match` (`on|off`, off by default) matches the mesh to the bed temperature of prints started
  elsewhere (slicer, touchscreen; #296). A new print on the mounted plate waits for its bed target and gets
  the nearest blended mesh in printer memory, or else the nearest usable measurement (then the selected
  nozzle, the side slot's, the newest), loaded like a measurement chosen in the print dialog; a print CC2
  Control started keeps the dialog's choice. With it on, CC2 Control writes up to 4 blended profiles
  `cc2b_<plate>_<temp>` for the mounted plate into `autosave.cfg` while the printer is idle, on 5 °C steps
  where the nearest mesh would be furthest off (largest point difference first), each point linear between
  the two measurements around it, same grid only, never beyond the measured range. A blend is used only
  once a `bed_mesh.profiles` query shows it in memory with exactly those points, i.e. after the next
  printer restart; `/api/plates/blends/restart` (empty body) restarts the printer for that when blends wait.
  Turned off, CC2 Control takes every `cc2b_` profile out of `autosave.cfg` again while the printer is idle
  (the printer keeps any it loaded until its next restart, unused).
  `GET /api/plates` reports `match` (`enabled`, `result` of the latest print: `armed`, `armed_blend`,
  `slot`, `off`, `no_measurement`, `no_bed_target`; `blend_temp` of a blend in use; `blends`: `state`
  `off`, `too_few`, `planned`, `written`, `ready` or `write_failed`, and `temps` with `live`) and
  `print_mesh.source` (`dialog` or `auto`).
- `/api/plates/save` (`A|B`, name, Z, temperature, nozzle; the last two optional, 60 °C and none) stores the
  mesh the printer keeps for that side as a new plate with that measurement. The slot in `autosave.cfg` and
  in printer memory must agree.
- `/api/plates/measure` (plate, temperature, nozzle) after a calibration on that plate: the side slot becomes
  its measurement at that temperature (replacing one with the same temperature and nozzle), the one it
  mounts with, and the plate is mounted in place. `/api/plates/recapture` (id) does the same for the mounted
  plate with the temperature and nozzle of its current measurement. `/api/plates/measure/delete` (id) removes
  a measurement and its printer profile; a plate keeps at least one. `/api/plates/measures/clear` (plate id)
  is a function of its own that deletes every measurement of the plate and their printer profiles on purpose: the
  plate stays as a profile without a mesh (`{"cleared":n}`) and cannot be mounted until it is calibrated again. It
  answers 400 for an invalid id, 404 for an unknown plate and 409 for the mounted plate, one waiting for the
  printer restart and one that is being calibrated. `/api/plates/measure/edit` (id,
  temperature, nozzle) corrects what a measurement records, e.g. the nozzle of one made before nozzles were
  listed; two measurements of a plate cannot share temperature and nozzle. `/api/plates/keep` (`A|B`) stores
  the printer profile of the measurement the side slot holds.
- `/api/plates/calibrate` (`A|B`, temperature, soak minutes 0–60, nozzle, plate; the last two may be empty;
  an optional sixth line `work`, only together with a plate, any other sixth line is 400) runs a bed mesh calibration on CC2 Control, so no page has to stay open. An open screw-tramming session does not
  block it between its measurements: the session ends (`end_reason` `mesh_calibration`, its heaters are not switched
  off, the calibration takes the bed over) and the calibration starts; while a measurement of the session runs the
  answer is 409 (`A screw session measurement is running`), and while a hold time before a screw measurement runs it
  is 409 as well (`A screw measurement is waiting for its bed hold time`). While the calibration runs, `/api/control`
  refuses every screw measurement (409, `A bed mesh calibration is running`); a four-corner measurement or a session
  start right after it, at its temperature, follows it directly (see "Directly after a bed mesh"). The state reports `elapsed`, the seconds since the
  current stage began, so a page opened later can count the probing up. It keeps the slot's measurement as
  a profile, homes the printer when needed (`G28`), heats the bed (`M140`), waits until telemetry reads the
  temperature, holds it for the soak time (the firmware's own calibration probes as soon as the sensor
  reads the target, while the plate still expands), then sends `BED_MESH_CALIBRATE PROFILE=<slot>
  BED_TEMP=<t>` through the console. The firmware switches an idle printer's bed off 15 minutes after the
  last command, so while it heats and soaks the run sends `M140` again every ten minutes, and only while
  the bed still holds the target the run set. With a plate, the result becomes its measurement as with
  `/api/plates/measure`. With `work` it becomes only the plate's temporary mesh: CC2 Control keeps the slot
  mesh in memory (`result` `work`) and leaves the library as it was: the library file, the mounted plate, the
  measurement it mounts with, its Z offset and the printer profiles stay as they are. The side slot of the printer,
  in its mesh file and in its memory, holds the temporary mesh afterwards, as after every calibration, so the plate's
  stored measurement is no longer in the printer (`in_printer` of the plate is `false`); the slot's measurement keeps
  its profile as for every calibration, so a print started from the print dialog loads its stored measurement from
  that profile. The temporary mesh is never offered to a print, the mesh matching or the blends. It stays while
  a new calibration heats, soaks and probes, also when that run is cancelled or fails before its probing has ended,
  and is gone once a calibration has finished its probing (the slot then holds another mesh; a new `work` run then
  keeps its own), after a restart of CC2 Control, or after a measurement of the same plate stored in the library;
  `work_mesh` is `null` while its plate is not in the library. `GET /api/plates` reports it as `calibration` (`stage` `homing`, `heating`,
  `soaking`, `probing`, `saving` or `off`, the soak seconds `remaining`, the bed temperature `bed`, and
  `result` `done`, `saved`, `work`, `failed` or `cancelled` with `error` `homing`, `heating`, `busy`, `telemetry`,
  `probing` or `saving`; `work` whether the run keeps only a temporary mesh, and `work_mesh`, `null` or
  `{"plate","temp","nozzle","measured","mesh"}` while its plate is in the library). At the end of the soak the probing starts only when MQTT has reported Idle within
  15 s and fresh telemetry reads the bed at its target; otherwise it waits up to a minute and then stops.
  `/api/plates/calibrate/cancel` stops it until the probing starts. A run that is cancelled or fails switches
  the bed heater off only while fresh MQTT reports Idle and fresh telemetry shows the target the run set;
  otherwise, such as for a print another client started meanwhile, it leaves the heater as it is. Print
  starts and plate changes are refused while it runs; a print started meanwhile, or a changed bed target,
  ends it. After the probing, reading the slot mesh (its file and the printer memory) is tried again for up to 60 s, at
  most every 2 s, while it cannot be read yet or memory and file still differ; the mesh is kept only when both
  agree, and any other error while saving ends the run at once (`error` `saving`).
- `/api/plates/mount` (id, optionally a measurement id) mounts a plate whose measurement is already in its
  slot and applies its Z offset with `SET_GCODE_OFFSET Z=` (no movement). Any other answers 409 with
  `"reboot_required": true`; adding a line `REBOOT` writes the slot (the previous file becomes `autosave_backup.cfg`, a copy
  is kept as `bed-plates.json.autosave.bak`) and reboots the printer. The next CC2 Control process checks the
  slot in the file and in printer memory before it reports the plate mounted. If the reboot cannot be
  started, the previous mesh section is restored while unrelated configuration changes are preserved.
- `/api/plates/edit` (id, name, Z) renames a plate or changes its Z offset; the mounted plate's new offset is
  applied at once when the printer is idle.
- `/api/plates/nozzle` (id or empty for a new one, name, diameter, Z correction), `/api/plates/nozzle/delete`
  (id) and `/api/plates/nozzle/select` (id or empty) manage the nozzles; a new correction of the installed
  nozzle is applied at once when the printer is idle. `/api/plates/delete` (id) also removes the plate's
  printer profiles; `/api/plates/unmount` changes only the library.

Changes that touch the printer require a connected, registered, idle printer with fresh MQTT and printer
service telemetry, no command or file transfer of CC2 Control in progress and no pending restart. The mounted
plate's Z offset is applied again once the printer is idle after CC2 Control starts or the printer service
restarts, which CC2 Control recognises by a new `/tmp/elegoo_uds` socket file; a reconnect to the same service,
such as after a receive timeout during a busy print start, keeps it applied. Names are
1–64 UTF-8 bytes without quotes, backslashes or control characters; a library file that cannot be read keeps
the library closed rather than being overwritten. Requires printer validation.

### Plate recovery safeguards

An immediate mount requires the saved mesh to match both `autosave.cfg` and the native
printer profiles. A disk-only match requires the same explicit reboot confirmation as
a changed mesh. The delayed reboot guard requires connected, registered MQTT with
idle status received within 15 seconds, fresh UDS telemetry, and no active console,
upload, download, or pending Z adjustment.

Automatic post-restart verification and Z restoration use nonblocking UDS exchanges
with a two-second deadline and a 128 KiB reply limit (every stored profile is part of the reply). Failed exchanges retry after five
and fifteen seconds, then stop after three failures until the plate selection/value
or the printer service changes. A reconnect to the same service does not reset this
budget. A failed pending mount is cleared and must be mounted again explicitly. Conflicting manual commands and print starts return 409 while an exchange
is active; emergency stop remains available. Explicit plate actions retain their
existing bounded synchronous native queries.

If a reboot fails, rollback restores only the mesh section written by the mount,
preserving unrelated configuration updates. If that section changed again, cannot
be read, or cannot be restored, the library becomes unavailable and retains the
pending mount and backups for recovery instead of reporting a successful rollback.

Host regressions cover disk/memory disagreement, stale reboot guards, concurrent PID
updates, failed rollback, bounded retries across reconnects, and fragmented/late UDS
replies. These checks do not substitute for validation on the physical printer.

## Spool tracking

CC2 Control keeps a filament inventory in `spools.json` (`--spools FILE`, next to the plate library). Each
spool sits in one place: a Canvas slot 0–3, the external spool holder (slot 4), or storage. Tracking is off
until it is switched on; the inventory can be edited either way.

Consumption is measured, not taken from slicer estimates. The UDS subscription carries
`print_stats.filament_used`, the net extruder travel of the current print in millimetres (the vendor port of
Klipper: purges and filament changes are included, moves made while paused are not, retractions count
negative), and in the same stream the Canvas channel that feeds the extruder, `canvas_dev.active_cid`.
While MQTT reports `printing` or `paused`, every change is charged to the spool in that tray; `-1` without
Canvas trays is the external holder. Between two trays the old filament is cut and pulled back without
extruder moves that count, and the extruder pulls in the next one, so what it moves then waits for the tray
named next (when the print ends first, the last tray takes it). MQTT's `canvas_info.active_tray_id`
stands in only when the UDS stream lacks the channel: it reports a colour change seconds late, when much of
the new colour's purge would already be charged to the old spool. The printer names the first tray of a
print only once it has loaded it, so what the load extruded is then moved from the external holder to that
tray. Millimetres become grams with that spool's diameter and density and the current
`gcode_move.extrude_factor`. A print that is cancelled or fails is therefore charged with what it really used.
The end of a print is logged 8 s after MQTT reports it, so the last readings are counted, with the result
UDS `print_stats.state` gives (MQTT can drop the file name before it reports the end); a restart of
CC2 Control continues the count of a print in progress from the saved file, and the same file printed again
is recognised by `total_duration` starting over. A print joined more than five minutes after its start is
counted from then.

The Canvas reports a status per tray: 0 no filament at its feeder, 1 inserted and pre-loaded, 2 feeding the
extruder. CC2 Control compares every tray report with the previous one:

- filament inserted (0 → 1/2) or changed on the touchscreen opens a question for that tray, shown on every
  open page through `/api/printer` `spools.questions`. The tray stays unbound until it is answered; what it
  extrudes meanwhile is kept (`question_mm`) and charged to the spool that is chosen. Filament put back
  after any time out of the tray asks as well: another spool goes in within seconds, and the tray keeps
  reporting the filament it had until someone edits it; the spool that was there is kept as `last`;
- within 30 s of an assignment, a tray report counts as that spool's own filament, so writing the spool's
  material and colour to the tray asks nothing;
- a tray that stays empty for 15 s gives its spool back to storage;
- a tray that empties while it feeds a print (2 → 0) has run out: once the printer has moved to another tray,
  the print has ended or new filament is put in, its spool is set to zero and the log shows how far the
  count was off.

Switching tracking on checks every binding against the trays, so spools swapped while it was off are asked
about again instead of being charged for another spool's filament.

A tray report fits a spool when the colour is the same and the spool's material equals the tray's type or
name, or is a product line of that type: `PLA Matte`, `PLA-CF` and `PLA+` fit a tray that reports `PLA`.
A change to a filament that still fits the bound spool asks nothing.

Endpoints (changes are `POST` with `key=value` lines and need `X-CC2-Request: 1`):

- `GET /api/spools` returns `enabled`, every slot with its spool, the spool it held before (`last`), an open
  question (`inserted` or `changed`), the printer's report of the tray, the print being counted with each
  tray's millimetres and grams, the spools, and the newest 150 log entries (prints, run-outs, corrections,
  additions).
- `/api/spools/enable` (`on` or `off`).
- `/api/spools/save` creates a spool (no `id`) or edits one: `name`, `brand`, `material`, `color` (`#RRGGBB`),
  `diameter`, `density`, `net`, `tare` (empty spool weight), `low` (warning level), `price`, `note`,
  `archived` (`0`/`1`). `remaining` is accepted only when creating; `slot` puts a new spool straight into a
  tray and answers its question.
- `/api/spools/assign` (`slot`, `spool`; an empty `spool` empties the slot). A spool sits in one place, so it
  leaves any other slot.
- `/api/spools/dismiss` (`slot`) answers a question with "no spool": the tray's use is not counted.
- `/api/spools/adjust` (`id` and `remaining` or `gross`): a weigh-in. `gross` subtracts the spool's `tare`.
- `/api/spools/delete` (`id`). Log entries keep the id.

Texts are UTF-8 without quotes, backslashes or control characters (name, brand and note up to 96 bytes,
material up to 32). The file is replaced atomically after each change, and while printing at most every two
minutes (sooner after 5 g); a file that cannot be read keeps the library closed rather than being
overwritten. `POST /api/gcode-files/inspect` adds each tool's slicer length (`mm`) when the file states
`; filament used [mm] = …`, so the print dialog can compare it with the spool the tool would draw from. It
also reports `bed_temperature` (the first `M190`/`M140` with a target) and `nozzle_diameter` (from the
slicer's configuration block), or `null`, for the build-plate measurement and nozzle the dialog suggests.
Requires printer validation.

### Spoolman sync

An optional bridge between the local spool library above and a self-hosted
[Spoolman](https://github.com/Donkie/Spoolman) server on the LAN. Spoolman is the inventory source of truth
for any spool you link to it: CC2 Control periodically reports the grams this printer has measured for it
(`PUT /api/v1/spool/{id}/use`) and pulls Spoolman's own material, colour, name and remaining weight back
into the local entry — the same way a refill or an edit made in Spoolman's own web UI would otherwise never
reach the printer.

- `POST /api/spoolman` (`url`, `basic_auth`) configures it. `url` must be `http://host[:port][/base]` —
  there is no TLS stack linked into this binary, so `https://` is refused. An empty `url` turns sync off.
  `basic_auth` is `user:pass` for a reverse proxy some self-hosters put in front of Spoolman; Spoolman
  itself needs no key. `GET /api/spools` reports `spoolman_url` and `spoolman_has_auth` (never the secret
  itself).
- Link a spool to Spoolman's own numeric spool id with `/api/spools/save` `spoolman_id` (digits only; a
  Spoolman spool can be linked to one local spool only, otherwise `409`; changing or emptying the id discards
  anything still queued or in flight for the old link, and replies to requests started under the old link are
  ignored). Changing the server URL or login does the same for every spool. Exported backups leave out the
  proxy login and the sync accounting, and a restore leaves sync off until it is configured again.
- Sync runs on the same cadence as the local library's own periodic save (at most once every two minutes),
  in a background thread — never on the request-handling thread, so a slow or unreachable Spoolman cannot
  stall the printer connection.
- Usage accounting. Spoolman's `/use` **adds** `use_weight` to the spool's used weight, so repeating a request
  whose outcome is unknown could charge the same grams twice. Each linked spool therefore keeps `unsent`
  (not handed to a request yet), `inflight` (handed to a request whose outcome is unknown) and the baseline
  `used_weight` read just before that request, all saved with the library and restored after a restart.
  A failure that cannot have reached Spoolman (no connection, request not fully sent, a 4xx) leaves the grams
  in `unsent` for the next cycle. A request that may have been applied (the connection dropped after the request
  went out, a 5xx, a truncated or unreadable reply) is **not** retried: the next read of `used_weight` settles
  it — it grew by the grams, so it was applied; it did not, so the grams return to `unsent`; it changed in some
  other way or the baseline is gone (a restart in the middle of a request), so the grams are dropped rather than
  risk charging them twice, and the pull brings the spool to Spoolman's own figure.
- A pull sets the remaining weight to Spoolman's figure minus what Spoolman has not been told about yet
  (`unsent` + `inflight`), so usage charged while a request was in flight is kept.
- Replies are read whole or rejected: headers and `Content-Length` or chunked framing are checked, a reply that
  is cut short is an unknown outcome, and a reply larger than its buffer (8 KiB for one spool, 128 KiB for the
  spool list) is refused, never truncated. Numbers and texts from Spoolman pass the same bounded parsers as
  local data; a value that fails is ignored.

## Bounded asynchronous G-code analysis

Metadata and inspection scans run on one worker with a 128 KiB stack and a queue
limited to four pending requests. Two fixed response caches occupy approximately
10 KiB and are invalidated when the file identity/stat changes. Files are scanned
in bounded buffers; the whole G-code is not loaded into memory. A full queue
returns an error instead of creating additional threads or unbounded work.

The same worker handles active-job G-code analysis so `/api/printer` does not
scan a large file on the HTTP/MQTT/UDS event loop. The current job returns cached
values as they become available; switching jobs prevents old results from being
published for the new file.

The browser allows up to 120 seconds only for metadata/inspect and rejects
duplicate preparation clicks while showing an analysis message. Other API
timeouts remain unchanged. No print starts before the operator confirms it.

HTTP receive handling probes sockets without blocking before expiring incomplete
requests. A complete queued request survives an event-loop scheduling stall; an
incomplete request still expires. This also covers the observed WSL1 readiness
behaviour without increasing the global HTTP timeout.

Host regressions cover 106+ MiB files, trailing metadata, cache invalidation,
active-job transitions, concurrent health/UDS traffic and scheduling stalls.
The owner reported successful large-file selection and an ongoing 108 MB print
with the experimental callback active. This CC2 change neither requires nor
modifies that module, but standalone printer validation without it is pending.

## Native printer reports

`GET /api/printer` also returns `printer_report`: null until a vendor event is
received, then `{event_id, sequence, code, level, message, age}`. This is the last received
event, not an assertion that a fault is still active. `age` is monotonic seconds.
It survives UDS disconnects but resets when CC2 Control restarts. `message` is
native text or null if missing, invalid or longer than 1023 encoded JSON bytes.
The same response includes `printer_alert`, the latest WARNING or CRITICAL
report in the same format. Informational and RESUME events still update
`printer_report` and leave `printer_alert` intact. The UI renders alert text and
remembers explicit dismissal across reloads. Consecutive identical native reports retain their event ID;
a warning following a RESUME is a new event, even with the same error code.
The vendor stream does not provide an authoritative occurrence ID, so repeated
identical warnings without an intervening transition cannot be distinguished.

The existing socket subscribes once per connection to `gcode/subscribe_report`.
No periodic error query is added. Ordinary code-zero INFO output does not replace
an event. WARNING (1), CRITICAL (2) and RESUME (3) retain vendor semantics;
a resume report does not establish that all faults are cleared. Unknown numeric
codes remain visible. MQTT request refusals stay separate in `printer_error`.
Reports do not refresh sensor telemetry. An unsupported subscription response
is ignored without disconnecting the status stream.

Protocol provenance: ELEGOO's published [webhooks.cpp](https://github.com/elegooofficial/CentauriCarbon2/blob/5a2ea7fc03e707552701b1a69f463699cbd39230/elegoo/webhooks.cpp)
and [error levels](https://github.com/elegooofficial/CentauriCarbon2/blob/5a2ea7fc03e707552701b1a69f463699cbd39230/elegoo/common/exception_handler.h).
This snapshot predates deployed firmware; real-printer event delivery remains
required validation. Displaying a report does not diagnose an unreported stop.

Physical validation: a simulated spaghetti-detection event displayed correctly;
the old message did not reappear after page refresh or a printer reboot.

### Reset all Canvas filament records

`POST /api/control` with body `canvas:reset-all` resets the material, colour and nozzle-temperature fields for slots 0–3 using the printer's native `CANVAS_SET_FILAMENT_INFO` commands. It does not unload filament or delete the spool inventory. The UI requests explicit confirmation and refreshes Canvas readback after submission.

Requires connected, registered MQTT telemetry no older than 15 seconds, a valid timestamp and a known Idle machine state. Printing, paused printing, missing/stale telemetry and invalid command length are rejected. The four-command batch fits the existing console limit; acceptance reports submission rather than a confirmed firmware result.

### Explicit custom plate mesh activation

The Build Plate Library selector lists each plate's saved measurements by side,
temperature and timestamp. Selecting an entry is inert. **Mount and activate**
asks the user to confirm the physically installed plate, then uses the existing
plate/measurement mount endpoint and its idle/fresh/transfer/calibration guards.
A changed side-slot mesh retains its explicit restart confirmation. The print
popup's official A/B selection and the graph's visualization selector are unchanged.

An immediate mount now loads its verified side slot and reads back the actual
`bed_mesh.profile_name`, `probed_matrix`, `mesh_min` and `mesh_max` before applying
Z and reporting mounted. A stored profile alone or an empty RPC acknowledgement
is insufficient. Pending mounts after restart perform the same checks through
bounded, nonblocking transactions before reporting success. Failed verification
clears the mounted claim; no mesh load is added to ordinary idle recovery after
a later print. The active profile label reflects printer telemetry. Print start
can still select the side slot or probe a new/adaptive mesh according to its
options; an idle selection is not a replacement for choosing the physical side.

Test qualification is separate from printer qualification. The regression model
keeps stored profiles and the active matrix independent and covers successful,
ignored/refused loads, missing/wrong matrix data, busy/stale guards and pending
post-restart activation. A controlled V4.4 printer trial is required before release.

## Experimental feature preferences

`GET /api/experiments` reports the stored choices, actual Canvas UDS availability,
editability and the shared cold/idle readiness guard.

`POST /api/experiments` accepts exactly `canvas-uds:on`, `canvas-uds:off`,
`quiet-motors:on` or `quiet-motors:off`, with `X-CC2-LAN`. Both default off and
are persisted independently. Changing Canvas reconnects the existing UDS connection,
revokes old direct-state authority and sends no motion/heating command. Disabling
Quiet controls, or changing Canvas while they are enabled, requires a successful
explicit Normal command on the current UDS connection. Invalid storage is not
overwritten. See [experimental features](EXPERIMENTAL_FEATURES.md) for limitations.

## Experimental bed heating

`GET /api/bed-drying` returns the same `bed_drying` state included in the existing
`GET /api/printer` poll: enabled, active, state, reason, temperature, minutes,
remaining (hold seconds), fresh, bed and target. `fresh=false` numeric samples
are not authoritative. The browser adds no extra printer poll.

`POST /api/experiments` accepts `bed-drying:on` / `bed-drying:off` using the
existing cold/idle/LAN guards. Enabling the UI heats nothing.
`POST /api/bed-drying/start` requires `X-CC2-LAN` and canonical text
`temperature:minutes` (30–90 °C, 1–720 minutes), fresh idle/cold status, zero
heater targets and no other operation. `POST /api/bed-drying/stop` uses the same
LAN header; receipt is not confirmation of heater shutdown. Confirm only after
zero target readback. The session cannot be extended or resumed via Start.
See [BED_DRYING.md](BED_DRYING.md) for native lease and recovery limitations.

`POST /api/bed-drying/preheat` starts the same session as a chamber preheat. It requires
`X-CC2-LAN` and canonical text `bed:chamber:minutes`: bed 60 °C up to the lower of 100 °C and the
printer's `[heater_bed] max_temp`, chamber 30–55 °C, and 1–240 minutes held once the chamber is
ready. It answers `409` unless the same idle checks pass (a warm bed with a zero target is allowed),
the exhaust grille state (`print_stats.cavity_temperature_mode`), aux fan and chamber temperature
are readable, and the chamber is below the target. The state then also reports `mode`
(`"bed"` or `"preheat"`), `chamber_target`, `chamber` (°C or null), `ready`, `levelled` (ready because the chamber stopped rising more
than 1 °C short of the target), `plateau` (the
estimated level the chamber is heading for, °C or null), `fan` (aux fan PWM 0–255 sent by the
session) and `bed_limit`. `state` is `"handed_off"` while a print that took over the bed runs.

## Print queue

Jobs prepared ahead of time: a G-code file, the print dialog's choices and a number of copies, kept in
`print-queue.json` (`--queue FILE`, next to the plate and spool libraries) and shown on the Queue page, the
Dashboard's current-job card and the print-ended notice. Every copy is one protected start through the same
code as `POST /api/gcode-files/print`, with the same guards; nothing starts without the operator's button
unless the `auto` policy is chosen.

- `GET /api/queue` returns `{"available","error","revision","printer_idle","policy":{"after":"wait"|"auto",
  "delay","remove_done"},"active","next","items":[…]}`. Items are in queue order with `id`, `storage`, `path`,
  `mapping`, `side`, `leveling`, `timelapse`, `measure`, `nozzle`, `copies`, `done`, `estimated_seconds` and
  `filament_grams` (`null` when unknown), `added`, `state` (`pending`, `starting`, `printing` or `done`),
  `last_result` (`""`, `stopped`, `not_started`, `auto_failed`, `auto_material` or `not_opened`), `large_ok`, `present` (the file is in place),
  `size` (the file's bytes, `null` when it is not there; the page waits for a start of the job as long as
  copying and checking that size may take)
  and `filaments`: one `{"tool","type","name","color","slot","use","reason"}` per tool of a job that prints from
  the Canvas (none for the external feed). `type`, `name` and `color` are the filament the tool is bound to, as
  a Canvas slot labels it (empty texts while it is not known yet); `slot` is the Canvas slot the job was added
  with, or `null` for a tool added for a filament alone; `use` is the slot a start would take now, the one that
  holds the filament, or `null` when none does; `reason` says what was found: `""`, `"empty"` or `"other"` (the
  slot the job was added with is empty, or holds another filament), `"absent"` (no slot holds the filament of a
  tool added without a slot) or `"unknown"` (the Canvas has reported no slots to look in). `mapping` holds the
  slots the job was added with (see below).
  `next` is the first pending job whose file is present; `active` the job whose copy is starting or printing;
  `auto_in` the seconds until a pending automatic start, else `null`.
- `POST /api/queue/add` takes the eight newline-separated fields of `/api/gcode-files/print` (storage,
  filename, Canvas mapping, plate side, leveling mode, timelapse, measurement, nozzle), then `copies` (1–99),
  `position` (`end`, or `next` to go to the head behind a running copy) and optionally the slicer's
  `estimated_seconds` and `filament_grams` (two lines, empty when unknown). Further lines bind tools to a
  filament by itself, which need not be in the printer: `tool<TAB>type<TAB>name<TAB>#RRGGBB`, one per tool,
  for tools the mapping does not name (`type` or `name` may be empty, not both; empty lines are skipped), and
  last a line `large_ok` for a file of 2 GiB or more (see *Files of 2 GiB or more*). A
  tool of the mapping is bound to the filament its slot is labelled with (see below). The syntax and the file
  are checked now; the printer-dependent checks (tools against the file, mesh, measurement, nozzle) run at each
  start. 201 `{"ok":true,"id","count"}`; the queue holds at most 32 jobs.
- `POST /api/queue/start` with a job id, or an empty body for `next`: refused (409) while a queued copy is
  starting or printing, when the job has no copy left, while plate verification runs, or while another
  session holds the printer (the refusals every print control shares: maintenance, the Canvas and filament
  tools, a camera cell, an application update, a managed connection, drying, the nozzle test), or when a
  filament of the job is in no slot (`Canvas slot N is empty and no other slot holds the same filament`,
  `No Canvas slot holds the job's filament: <name> <colour>`, or `The Canvas has not reported its slots, so the
  job's filament cannot be found`); otherwise it answers exactly as `/api/gcode-files/print` would (202
  accepted, or its 4xx/5xx), with every tool mapped to the slot that holds its filament. The job is saved as
  `starting` before the request goes to the printer, so a restart in between still follows the copy.
  A job fed from the Canvas takes its slots from a Canvas report that arrives after the start was asked for,
  not from the one a page showed, which may be older than a spool just changed: while the printer is heard
  from and its Canvas has reported once, the first request asks for a report (method 2005) and is answered
  409 `{"ok":false,"error":"Reading the Canvas slots before the start","checking":true}`, and so is every
  request for that job until the printer has answered; the request after the answer starts. A request more
  than 10 s after the one that asked, with no answer, asks again. The UI asks again every 0.5 s for up to
  12 s. The external feed starts at once.
  The job is then checked (and a USB file copied) on the start worker, as from the print dialog: CC2 Control
  keeps answering, the reply comes afterwards (the Queue page waits up to 300 s for it), and the job stays
  `pending`, also in `print-queue.json`, until then; a restart meanwhile leaves it pending. A job removed or
  another queued copy started meanwhile is not started (409). An automatic start copies the same way; if it is
  then refused or cancelled, the job gets `last_result` `auto_failed`.
- `POST /api/queue/filament` gives tools of a job other filaments: `id`, then a mapping line and filament
  lines as in `/api/queue/add`. A tool named in the mapping takes the filament that slot is labelled with now
  (and that slot as the one it was added with); a tool named in a filament line takes that filament and no
  slot. Tools the request does not name keep theirs; 409 for a tool the job does not have, 400 for a request
  that names none or one twice. A copy that is printing is not affected.
- `POST /api/queue/abandon` (optionally the job id) gives up a `starting` copy the printer never ran: only
  while the printer is idle with fresh telemetry (409 otherwise, or when no start is waiting); the job goes
  back to `pending` with `last_result` `not_started`.
- `POST /api/gcode-files/print` answers 409 `A queued copy is starting` while a queued start waits for the
  printer; a queued copy that is already printing is protected by the printer's own busy state.
- `POST /api/queue/remove` (id; never the running job), `/api/queue/reorder` (`id\nindex`, 0 is the head),
  `/api/queue/copies` (`id\ncopies`, never fewer than already printed, and for the running job at least one
  more; a total that equals the printed count finishes the job, which `remove_done` then drops),
  `/api/queue/policy` (`wait|auto\ndelay 10–3600\n0|1` for `remove_done`; a change never starts anything by
  itself, the automatic delay counts only from a copy counted while `auto` was on), `/api/queue/hold` (cancels
  the pending automatic start, the policy stays) and `/api/queue/clear-done`.
  The measurement and nozzle fields are plate library ids or empty. Every change is saved atomically before it
  is acknowledged; a failed save leaves the queue as it was.

Copies are counted on the backend, so a closed browser misses nothing. A `starting` job becomes `printing`
once fresh MQTT state shows the printer printing the file it was given (a USB import prints under its
`CC2_USB_…` copy); the printer service's file name is trusted only while it reports a running print, so a
name left over from the previous job cannot block the start. While the printer is busy with the start itself
(homing, bed mesh, heating beyond the targets it had when the start was requested) the wait goes on; three
idle minutes, another file printing for five seconds, or thirty minutes in all end it as `not_started`. A
copy counts when the printer is idle again with `print_stats.state` `complete`, the reported file is the
job's, and this copy's own job was seen running: the printer's job uuid changed from the one before the
request, or the printer service showed the file printing (a `complete` left over from the previous copy of
the same file, under the same uuid, does not count). `cancelled` or `error`, another job taking over (a new
uuid), or an idle printer without `complete` for a minute leave it uncounted (`stopped`); a pause, a filament
change or a recovery keeps it running. Telemetry older than 15 s decides nothing. The last copy sets `done`
and, with `remove_done`, drops the job. The `auto` policy starts the delay only if another pending copy has
its file present when the previous copy finishes. Removing the last waiting copy cancels its delay. A job
added to the empty queue waits for a manual start. When the delay expires, the next job starts if the
printer is idle and no other session holds it, else the job notes `auto_failed`.
Right before that start the backend checks the Canvas material the Queue page would warn about: every slot
the start would use must be present in fresh telemetry and hold the file's `filament_type` (the page's rule:
the slot's type contains the file's material, or the file's material contains the slot's first word); a
differing or unknown slot, a filament of the job that is in no slot, or a file without `filament_type`,
holds the copy
as `auto_material` for the operator, who can review the warning and start it by hand. The check takes only a
Canvas report received after the counted completion and after the delay ran out: the backend asks for one
(method 2005) at the completion and again when the delay has run out, keeps the Canvas watched while the
delay runs, and waits up to 10 s more for the second answer; when none arrives the copy is held the same
way. The external feed (a job without tools) has nothing to check. The policy is meant for a bed that clears
itself and is off by default. A queue file that cannot be parsed keeps the queue closed
(`available: false`) and is never rewritten. The file's `version` is the one a reader needs: 1, or 2 while a
job has a tool added for a filament alone, which a reader of version 1 would take for the external feed.

A job that prints from the Canvas is bound to filaments, not to slots. A filament is what a Canvas slot's
label names: `filament_type` (the material, "PLA"), `filament_name` (the product line, "PLA Matte") and
`filament_color`; a label without a material or without a colour names none. A tool added with a slot takes
the filament of that slot's label in the Canvas report when the job is added (an empty slot keeps the label
of the filament it held), or in the first report that has one; until then the tool prints with whatever its
slot holds. A tool added with a filament line is bound to that filament from the start, in whatever slot it
turns up. The queue knows a slot by its label only: a spool changed without changing the label is not seen.

A slot holds a filament when it is not empty (`status` 1 or 2) and its label has the same colour, and the
same type and name where the filament states them, compared without case: a filament stated without a name
is any product line of its type, which is how exact a label or a request can be. (The printer's own auto
refill, which acts during a print, also asks for the same type and colour.)

When a copy starts, by the button or automatically, each tool is mapped to the slot that holds its filament
in fresh Canvas telemetry: the slot the job was added with while it does, otherwise the lowest-numbered slot
that does, so tools that print with one filament may share a slot. The job itself does not change. A filament
that is in no slot refuses the start; the job waits until a slot holds it, or until `POST /api/queue/filament`
gives the tool another one. An empty slot of a tool that knows no filament yet is judged by the slot's own
label. Without fresh Canvas telemetry nothing is known against a slot: a tool added with one is sent on it,
and a tool added for a filament alone cannot be placed.

`filaments` in `GET /api/queue` reports the same decision ahead of the start, and the Queue page, the
Dashboard card and the print-ended notice show it. The printer answers a Canvas request and may not report a
changed spool by itself, so while a start from the Canvas is on offer (printer idle, no queued copy running)
every `GET /api/queue` keeps the Canvas watched for 15 s, as `GET /api/canvas` does: one 2005 request per
10 s, none during a print.

## Completed print readback

`GET /api/printer` adds `print.native` when the existing UDS cache has a fresh
print state (filename may be empty and duration null when cleared by firmware): `{state, filename, duration}`; otherwise null.
It adds no printer query. Raw `machine.progress` remains the native last-job
value; the current-job UI resets its progress when idle. The nonblocking CC2
notice accepts explicit completion only, deduplicates acknowledgements per
browser origin and opens normal file preparation for Print again. Dismissing
this notice does not acknowledge or operate the physical display.


Nozzle expansion: `POST /api/experiments` accepts `nozzle-expansion:on` / `nozzle-expansion:off` with the existing LAN authorization and idle/cold checks. The choice defaults to off and migrates schema-1/2 settings without enabling it. `GET /api/nozzle-expansion` adds `enabled`; start/factory require the enabled, valid experiment. Recovery and an already-required restart do not require opting in.

Screw pitch and alignment settings: `POST /api/experiments` accepts `mesh-align-settings:on` / `mesh-align-settings:off` with the same LAN authorization and idle/cold checks, and `GET /api/experiments` reports `mesh_align_settings`. It defaults to off. It only shows the settings block of the same name on Bed Levelling → Bed alignment (tilt span, the shape-warning and nearly-flat limits, screw positions, row order of the mesh and the screw pitch, 0.3 to 1.5 mm per turn, saved in the browser); without it the page uses the CC2 defaults, among them the CC2's own M4 screw (0.7 mm per turn, clockwise seen from below raises the corner). It sends nothing to the printer. The stored choices use schema 4; a schema-3 file keeps its choices with this one off.
