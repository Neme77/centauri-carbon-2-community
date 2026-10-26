# MQTT and dashboard workload

The UDS client adds a second telemetry source. Resource savings must be measured;
a lower system `MemAvailable` alone does not identify which process used memory.

## Changes

- `/api/printer` formats selected fresh UDS fields directly alongside MQTT fields.
  It no longer copies the whole `mqtt_client`, including its input, snapshot,
  diagnostic and Canvas buffers. Job counters still require matching filenames.
  Missing/stale UDS fields still fall back to the continuously updated MQTT cache.
- Received topics ending exactly in `/api_request` or `/api_register` are command
  echoes, not printer telemetry. They are discarded before JSON scans, diagnostic
  copying and state updates. They cannot renew printer-state freshness or change
  cached safety state. Other topics, including registration/API responses, remain
  processed. The wildcard subscription is retained for vendor compatibility.
- Extruder and bed objects are located once per processed MQTT payload instead of
  repeating object scans for each temperature, target and filament field.
- `/api/health` exposes lifetime `mqtt_received_publishes`, `mqtt_skipped_requests`
  and `mqtt_skipped_request_bytes` counters. Skipped bytes measure avoided payload
  analysis/copying, **not** saved network traffic. Counters reset with the process
  and may wrap on 32-bit platforms.

No new thread, periodic HTTP source, allocation or telemetry cache is added.
MQTT heartbeats, registration, Canvas discovery, subscriptions and commands remain
unchanged. Periodic startup snapshot requests already stop after Canvas discovery;
there is no continuous full-snapshot polling to remove in the steady state.
Chamber, Canvas, job state, safety and Panda retain their MQTT inputs.

## Printer replies, auto refill and print history

- Replies on CC2 Control's own `<client>/api_response` topic are scanned for
  `method` and `result.error_code`; a non-zero code is kept as `printer_error`.
- Canvas: one 2005 request per MQTT session while the printer has not reported
  `auto_refill` (discovery can complete from a status delta that omits it), and
  one after an accepted auto-refill change, to read the new value back.
- History: one 1036 request when the History page opens or Refresh is pressed,
  at most one in flight; nothing polls the printer. The printer lists its last
  50 jobs (19.6 KB, answered in 0.2 s on the tested printer), more than the
  16 KiB input buffer: a reply addressed to CC2 Control is assembled on the heap
  up to 256 KiB and freed after parsing; only the latest successful history
  (within the same bound) is kept. Large publishes for other clients are drained as before.
- Time-lapse rendering (1051) is sent only while Idle, one at a time. Downloads
  use the existing two-transfer worker limit (64 KiB stack, 16 KiB buffer).

## Host verification

Host C tests exercise fragmented and coalesced MQTT packets through `mqtt_process`,
request suppression, counters, preserved registration/snapshot/Canvas handling and
independent temperature/filament fields. HTTP tests verify fresh UDS overlay,
filename mismatch, stale fallback and unchanged source caches.

With host GCC `-Os -std=c11 -D_POSIX_C_SOURCE=200809L -pthread -fstack-usage`,
`printer_response` changed from 78,064 to 6,832 bytes of bounded stack (71,232 bytes
less) compared with the live-print-tuning parent. These are compiler estimates on
the host, not measured ARM RSS or CPU savings. Reproduce with `gcc ... -c
cc2-control/src/main.c -o /tmp/cc2-main.o` and inspect `/tmp/cc2-main.su`.

## Printer comparison

Record the installed binary hash and take two samples 60 seconds apart for each
build, before replacing the binary. Use the same G-code, print phase, browser,
webcam state and visible page. Repeat the idle comparison as well. Install or
roll back only while idle. Avoid restarting the printer or changing speed modes
between samples within one run.

Run on the printer:

```sh
CC2_PID=$(pidof cc2-control)
sha256sum /opt/usr/cc2-control/cc2-control
cat /proc/uptime
cat /proc/$CC2_PID/stat
grep -E 'VmSize|VmRSS|VmData|VmStk|Threads' /proc/$CC2_PID/status
ls /proc/$CC2_PID/fd | wc -l
wget -qO- http://127.0.0.1:8081/api/health
wget -qO- http://127.0.0.1:8081/api/uds
```

CPU use is the change in `/proc/PID/stat` user+system ticks (fields 14+15), divided
by elapsed uptime and the kernel clock-tick frequency. If `getconf CLK_TCK` is
unavailable, compare ticks/second without inventing a frequency. Confirm the PID
is unchanged between each pair of samples. Track RSS, stack and file descriptors
separately from system free memory and load average.

During hardware validation check normal printing, Pause/Resume, chamber and Canvas,
speed/flow readback and upload-only during printing. A skipped-request count rising
with one stable UDS connection demonstrates suppressed parsing; it does not prove
lower total CPU. Real-printer resource and functionality results remain pending.

This change depends on UDS telemetry and live-print tuning. Further reductions in
broker traffic or printer work require confirmed vendor topic/subscription behavior
and a separate comparison; removing MQTT fallback fields now would leave stale
values when UDS disconnects and break other consumers.
