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
    "canvas_auto_refill": true
  },
  "endpoints": {
    "printer": "/api/printer",
    "health": "/api/health",
    "canvas": "/api/canvas",
    "files": "/api/gcode-files",
    "console": "/api/console",
    "preferences": "/api/preferences",
    "history": "/api/history",
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
