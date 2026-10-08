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

## Canvas auto refill

- `GET /api/canvas` adds `"auto_refill": true|false`, or `null` until the printer has reported the setting
  (full Canvas replies carry it; status deltas may not).
- `POST /api/canvas/auto-refill` with body `on` or `off` sends method 2004. It answers `409` while the printer
  has not reported the setting. After the printer accepts the change, CC2 Control requests the Canvas state
  again so the new value is read back from the printer.

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

## G-code file list and uploads

`GET /api/gcode-files` lists each storage (`internal`, `usb`) as
`{"available":…,"truncated":…,"count":…,"total":…,"files":[…]}`, newest first. A storage
lists at most 128 files: when it holds more, the newest 128 are listed, `truncated` is `true`
and `total` counts every G-code file found. The scan examines at most 4096 directory entries
per storage; beyond that `truncated` is `true` and `total` covers only the entries examined.

`POST /api/gcode-files/upload` never overwrites a file. When it rejects an upload after the
headers, for example with 409 because the name exists, it first reads the rest of the
request body, so a browser receives the error instead of a reset connection.

## Timelapse print option

`POST /api/gcode-files/print` accepts an optional sixth newline-separated field after storage,
filename, Canvas mapping, plate side and leveling mode: `1` enables timelapse for this print,
`0` disables it. Omission defaults to `0`; other values are rejected with 400.
The print popup exposes this selection for each job, initially unchecked.

Saved-mesh starts include the flag as native method 1020 `config.delay_video`.
Calibrated starts first send method 1019 with `params.config.delay_video` and a unique request ID.
A matching successful acknowledgement is required within 1.5 seconds before the existing calibrated
G-code start runs; rejection, disconnect or timeout prevents the start. Both modes require printer validation.

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

## Build-plate library

CC2 Control keeps named plate surfaces in `bed-plates.json` (`--plates FILE`, next to the UI preferences):
each one has a side, the 11 × 11 mesh the printer measured for it and a Z offset of up to ±1 mm.
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

Endpoints (all changes are `POST` with a text body, one field per line, and need `X-CC2-Request: 1`):

- `GET /api/plates` returns the library, which plate is mounted, whether its Z offset has been applied, a
  mount waiting for the printer restart, the last mount result (`mounted`, `verify_failed`,
  `reboot_failed`) and, per plate, whether its mesh is the one now saved for its side (`in_printer`).
- `/api/plates/save` (`A|B`, name, Z) stores the mesh the printer keeps for that side as a new plate. The
  slot in `autosave.cfg` and in printer memory must agree.
- `/api/plates/mount` (id) mounts a plate whose mesh is already in its slot and applies its Z offset with
  `SET_GCODE_OFFSET Z=` (no movement). Any other plate answers 409 with `"reboot_required": true`; sending
  the id and a second line `REBOOT` writes the slot (the previous file becomes `autosave_backup.cfg`, a copy
  is kept as `bed-plates.json.autosave.bak`) and reboots the printer. The next CC2 Control process checks the
  slot in the file and in printer memory before it reports the plate mounted. If the reboot cannot be
  started, the previous mesh section is restored while unrelated configuration changes are preserved.
- `/api/plates/edit` (id, name, Z) renames a plate or changes its Z offset; the mounted plate's new offset is
  applied at once when the printer is idle.
- `/api/plates/recapture` (id) gives the mounted plate the mesh now saved for its side, after a new
  calibration. `/api/plates/delete` (id) and `/api/plates/unmount` change only the library.

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
with a two-second deadline and a 64 KiB reply limit. Failed exchanges retry after five
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
While MQTT reports `printing` or `paused`, every change is charged to the spool in that tray (between two
trays the last one, `-1` without Canvas trays the external holder). MQTT's `canvas_info.active_tray_id`
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
  extrudes meanwhile is kept (`question_mm`) and charged to the spool that is chosen;
- within 30 s of an assignment, a tray report counts as that spool's own filament, so writing the spool's
  material and colour to the tray asks nothing;
- a tray that stays empty for 15 s gives its spool back to storage; an empty report shorter than that with
  the same filament afterwards is ignored;
- a tray that empties while it feeds a print (2 → 0) has run out: once the printer has moved to another tray
  or the print has ended, its spool is set to zero and the log shows how far the count was off.

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
`; filament used [mm] = …`, so the print dialog can compare it with the spool the tool would draw from.
Requires printer validation.

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
The UI renders text, never HTML. Only WARNING and CRITICAL produce an alert;
RESUME reports remain available in the API. The browser remembers the last shown
event across reloads. Consecutive identical native reports retain their event ID;
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
