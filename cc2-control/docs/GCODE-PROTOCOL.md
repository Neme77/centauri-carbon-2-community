# CC2 G-code file and print-start protocol

CC2 Control 1.1.15 reads files directly from the two storage roots used by the
printer firmware:

- internal: `/opt/usr/gcode/local/`
- USB: `/mnt/exUDISK/`

The browser receives relative paths only. The backend rejects absolute paths,
dot segments, control characters, symbolic links, non-regular files and names
without a `.gcode` extension. Scanning is limited to four directory levels and
128 files per storage root.

Print start uses the firmware's native MQTT request rather than injecting a
Klipper console command:

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

Ordinary G-code starts use `print_layout:"A"`. Hardware testing showed that
changing this field to `B` for a normal single-layout G-code is rejected by the
printer with error 1026, including when the request originates from ELEGOO's
own dashboard. It is therefore a file/package layout identifier, not a physical
build-plate-side selector. CC2 Control does not expose an unsupported A/B
choice. `bedlevel_force:true` requests a new leveling pass.

Adaptive leveling is different: its bounds are encoded in the selected G-code with a
`BED_MESH_CALIBRATE` command carrying `FROM_SLICER=1`, `ADAPTIVE=1`, or bounded
`MESH_MIN`/`MESH_MAX` coordinates. Inspection reports this capability to the
browser. The start request still gates probing through `bedlevel_force`: false
keeps the existing mesh, while true runs the adaptive command when bounds are
present or traditional full-bed leveling when they are absent. CC2 Control
therefore exposes only the probing mode that matches the selected file.

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
