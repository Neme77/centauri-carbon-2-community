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
  "community_firmware": "4.1",
  "cc2_control": "1.1.25",
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
    "persistent_ui_preferences": true
  },
  "endpoints": {
    "printer": "/api/printer",
    "health": "/api/health",
    "canvas": "/api/canvas",
    "files": "/api/gcode-files",
    "console": "/api/console",
    "preferences": "/api/preferences",
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
