# Active-print time estimates

Both OrcaSlicer 2.4.2 and ElegooSlicer 1.2.0.17 files tested here contain
`estimated printing time (normal mode)` near the end of the file. The existing
metadata endpoint reads these values correctly: 1h 9m 56s (4196 seconds) and
25m 47s (1547 seconds), respectively.

The active printer response previously used only MQTT remaining time. If the
printer did not provide a positive remaining time, the dashboard had no useful
finish estimate even though file inspection knew the slicer estimate.

For a connected, registered, fresh active printing/paused job, `/api/printer`
now falls back to the active G-code estimate minus elapsed print duration when
MQTT remaining time is zero or negative. Fresh matching-job UDS elapsed time
has the existing precedence over MQTT elapsed time. Positive MQTT remaining
estimates retain precedence. The response adds `print.remaining_source` with
`mqtt`, `gcode` or `unavailable`, without changing the MQTT cache or safety state.
No file estimate is applied to a terminal/Idle, disconnected, unregistered or
stale job. Missing file metadata remains unavailable; a changed filename resets
the cached estimate. Negative results are clamped to zero.

The fallback shares the existing active-file layer scan and filename cache; it
adds no periodic HTTP request or repeated full-file scan. The metadata scan now
also runs once if MQTT already supplied the layer count. This is a normal-mode
slicer estimate, not an exact completion promise. Heating, firmware behavior,
pauses and changes of speed mode/manual speed can affect actual completion. No
inference from byte progress or an automatic speed correction is introduced.

Host HTTP tests cover both time formats, MQTT precedence, layer precedence,
source-cache preservation, stale state, filename changes/missing files, expired
estimates and completed jobs. The actual supplied G-code files were also parsed
through the unmodified metadata endpoint to verify their durations. Printer
validation and an updated ARM beta payload remain pending.
