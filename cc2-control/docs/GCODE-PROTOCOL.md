# CC2 G-code file and print-start protocol

CC2 Control 1.1.15 reads files directly from the two storage roots used by the
printer firmware:

- internal: `/opt/usr/gcode/local/`
- USB: `/mnt/exUDISK/`

The browser receives relative paths only. The backend rejects absolute paths,
dot segments, control characters, symbolic links, non-regular files and names
without a `.gcode` extension. Scanning is limited to four directory levels and
128 files per storage root.

An ordinary start with an existing saved mesh uses the firmware's native MQTT
request:

```json
{
  "method": 1020,
  "id": 1020,
  "params": {
    "storage_media": "local",
    "filename": "example.gcode",
    "config": {
      "delay_video": false,
      "printer_check": false,
      "print_layout": "A",
      "bedlevel_force": false,
      "slot_map": [
        {"t": 0, "canvas_id": 0, "tray_id": 1},
        {"t": 1, "canvas_id": 0, "tray_id": 3}
      ]
    }
  }
}
```

`print_layout:"A"` selects the saved `default` mesh and `print_layout:"B"`
selects `default1`. The local MQTT API routes method 1020 directly to
`INTERNAL_START_PRINT`; although it accepts `bedlevel_force:true`, this path
does not execute the preparation stage used by ELEGOO LAN/RTM `START_PRINT`.

Calibrated starts therefore reproduce the full preparation locally: they set
the print surface and slicer-calibration flag, configure the Canvas map and
start with `SLICE_CFG_MODEL=0`. Slicer bounds in `BED_MESH_CALIBRATE` produce
an adaptive mesh. If the selected Side A/B profile is absent or is not a full
11x11 mesh, CC2 Control first runs a full calibration into `default` or
`default1`, respectively, then starts the file with slicer calibration disabled
for that first run.

USB requests use `storage_media: "u-disk"`. The complete field shape and method
number were cross-checked against ELEGOO's Apache-2.0 `elegoo-link` LAN SDK and
the symbols/strings in the target CC2 printer service. The HTTP endpoint refuses
the operation unless MQTT is registered, telemetry is present, the printer
reports machine status 1 (Idle), and the selected file still passes validation.

Before opening the confirmation dialog, CC2 Control streams the selected G-code
and detects the `T0`…`T15` tools used outside comments. A file without an
explicit tool command is treated as `T0`. The dialog shows the current four
Canvas trays and requires one physical spool selection for every detected tool.
The backend re-inspects the file when Print is confirmed and rejects missing,
duplicate or stale mappings.

`t` is the G-code tool number, `canvas_id` is `0` for the CC2 Canvas, and
`tray_id` uses the zero-based tray identifiers reported by method 2005. An empty
`slot_map` remains available only when the user explicitly selects the external
or default filament path for a single-filament job. Multicolour files require a
complete Canvas mapping.
