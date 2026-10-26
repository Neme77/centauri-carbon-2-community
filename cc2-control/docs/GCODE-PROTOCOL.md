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
start with `SLICE_CFG_MODEL=1`, bypassing the vendor preliminary calibration.
`EXECUTE_CALIBRATE_FROM_SLICER=1` enables the bounded calibration command
inside the file independently of `SLICE_CFG_MODEL`. The archived printer
log from 2026-09-27 shows `SLICE_CFG_MODEL=0` entering `Start print calibration`
and invoking `BED_MESH_CALIBRATE` with `from_slicer 0`. Slicer bounds in `BED_MESH_CALIBRATE` produce
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

## External feed and the Canvas colour table

An empty `slot_map` does not reset the printer's Canvas colour table, which maps
each tool to a Canvas channel. The firmware keeps the table of the last Canvas
print, also across reboots: the Canvas record stores it as `color_table0`,
`color_table1`, … and `color_table_size`. For an empty map, method 1020 only logs
`Empty print slot map`. At print start, M6211 looks each tool up in that table and
switches Canvas to the channel it finds. A table left by an earlier two-colour
print therefore sends `T0` to that print's channel. In the printer log of
2026-10-14 this paused an externally fed print at layer 0. With the Canvas cable
unplugged, the switch failed with `current canvas lost` and error 1210. With the
cable connected, the cutter cut the directly fed filament and the printer stopped
with error 1220.

The touchscreen clears the table before a single-filament print: its log of
2026-09-14 shows `CANVAS_SET_COLOR_TABLE T= CHANNEL=` before `SDCARD_PRINT_FILE`.
CC2 Control does the same before every start with an empty map, for saved-mesh
and calibrated starts alike. Once the request has passed every other check, it
sends that command once as `gcode/script` on `/tmp/elegoo_uds` and waits up to
two seconds for the reply. Method 1020, or the calibrated preparation, follows
only when the reply carries a `result` and no `error`. Otherwise the request
fails with 503 and nothing is started. This is one request per print start, not
a poll (docs/UDS_TELEMETRY.md). Starts with a Canvas mapping send no extra
request.

Clearing changes only the table in memory, so it is repeated before every
external start. In the printer log of 2026-10-14 the touchscreen cleared the
table at 01:06, but the Canvas record read at the next boot still held the old
map, and the printer used it again.

In the V4.2-R5 firmware (`Canvas::CMD_canvas_set_color_table` in
`libelegoo_extras.so`), the command reads comma-separated `T`, `CHANNEL` and
`COLOR` lists. Whenever `T` is given, it first empties the table, and a single
empty `T=` adds no entry. A tool missing from the table gets channel −1 from
`get_T_channel`, and M6211 then does not switch Canvas.

Because every call replaces the whole table, a map must go in one command. Method
1020 (`APIPrinter::set_print_tray_map` in `elegoo_printer`) sends
`CANVAS_SET_COLOR_TABLE T=0,1 ID=0,0 CHANNEL=2,3`; the handler ignores `ID`.
Calibrated starts send the same single command. CC2 Control 1.2.3 and earlier
sent one command per tool, so a multicolour calibrated start kept only the last
tool's channel.
