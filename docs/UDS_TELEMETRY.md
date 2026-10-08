# Local UDS telemetry

CC2 Control subscribes to `/tmp/elegoo_uds` using `objects/subscribe`. The
subscription is read-only and uses the existing HTTP/MQTT event loop. No G-code
is sent by this telemetry client. The console keeps its independent connection.

The initial response and subsequent `cc2_status` notifications populate a bounded
cache. Notifications contain changed fields only. Initial responses arriving
after a newer notification fill missing fields without replacing newer values.
The client handles split/coalesced ETX-delimited frames, disconnects on oversized
or invalid frames, and retries connections at most once every five seconds.
Only a stream that has been silent for three seconds gets a read-only `info`
request, at most one every two seconds, so the five-second receive timeout still
separates an unchanged cache from a dead peer. A healthy printer sends about two
notifications per second even when idle, because temperatures are reported to
0.01 °C, so in practice no request is sent. See "Firmware request retention".

`GET /api/uds` exposes connection/freshness status and nullable values, including
speed/flow factors, live velocity, fan fractions/RPM, progress and elapsed times.
`print_stats.filament_used` (`filament_used`, net extruder travel of the current
print in mm) feeds spool tracking; like the other job counters it is forgotten when
the print's file name changes.
`--uds-socket PATH` selects a socket for testing; the default is
`/tmp/elegoo_uds`.

`/api/printer` prefers fresh UDS nozzle/bed temperatures and fan readings. It
preserves the existing 0..255 fan scale. Layer, progress and print duration are
used only when the UDS filename matches the MQTT job (the UDS `local/` prefix is
removed for comparison). Missing/stale values retain MQTT behaviour. Job state,
identity, safety guards, chamber, auxiliary/box fans and Canvas remain MQTT-based.
The physical identity of `fan_generic fan1` still needs validation, so its values
are exposed as `fan1`/`fan1_rpm` only in `/api/uds`. A null total layer
count is not guessed; the existing G-code metadata fallback remains in place.
UDS `total_duration` is exposed as `total_elapsed`; it is not an ETA.

## Firmware request retention

The printer's `elegoo_printer` keeps every request it receives on
`/tmp/elegoo_uds` in memory: the request text, the reply fields and a reactor
callback are never freed, about 1.5 KB per request. Measured on 2026-10-06 with
V4.2-R5 (`elegoo_printer` MD5 `06071f7b3ca6f809d5a329e1a9466f4c`, Klippy
`v0.12.0-458-gd886c176`):

- two dumps of the firmware heap taken three minutes apart held 2431 and 2518
  copies of the heartbeat request, one for each request sent since boot, and
  the heap grew by 128 KB in between;
- every other method sent on the socket (`objects/query`, `gcode/script` and the
  touchscreen's own requests) was retained the same way;
- CC2 Control's MQTT pings left no copies, MQTT status polling at about 17
  requests per minute did not change the growth rate, and the subscription's
  notifications were not retained.

The first version of this client sent `info` every two seconds, around the clock.
On the 111 MB printer without swap that grew the firmware by about 3 MB per hour,
and `elegoo_printer` died about 11 hours after each boot. On 2026-10-05 the
kernel's OOM killer stopped it while the printer was idle. On 2026-10-06 it died
while a time-lapse render needed memory. Do not poll this socket. Read changing
values from the subscription, and send one-shot requests only for user actions
or once per job. For the same reason the subscription carries
`exclude_object.excluded_objects` and `current_object` for the Job page, which
used to query them every two seconds; the object list itself, which can reach
hundreds of kilobytes, is still queried once per job.

This first stage does not change UI controls, MQTT request schedules, or Panda
telemetry. It does not yet establish a CPU/RAM reduction. Compare those metrics
on the printer before removing redundant MQTT requests in a follow-up change.

## Validation

Host regression tests cover initial/delta ordering, field preservation, scaling,
job matching, stale fallback, fragmented frames and disconnects. A fake-peer
integration test exercises the real HTTP event loop and reconnection. Environments
that prohibit AF_UNIX socket creation explicitly skip that integration test.

Before merging, install the ARM build while idle and validate `/api/uds` and
`/api/printer` during printing, pause, cancellation and return to idle. Compare
layer, temperature, progress and fan values against the display. Confirm chamber,
Canvas and the console still work. On a test instance with a disposable socket,
verify fallback when the peer is stopped and cache reset on reconnection. Do not
remove or replace the printer's live socket to simulate a failure.

## Hardware validation follow-up

The first ARM build passed WSL tests (including the fake UNIX peer integration)
and printer checks for fresh UDS data, temperatures, fan RPM, matched job layer
and duration, and 50%/100% speed changes from the display. Pause/resume from the
display was reflected in MQTT job state. Repeated resets of the per-connection
message counter revealed reconnects; their cause remains unverified.

`/api/uds` now includes lifetime `connections` and `disconnects`,
`last_disconnect` (a fixed reason identifier), and `last_errno`. These counters
survive reconnects and reset only on process startup. No payload logging is
added. Hardware retesting must identify and resolve recurring disconnects before
merge. The job UI also recognizes `print.state=paused` when the machine status
remains Printing, enabling Resume and disabling Pause. The rebuilt UI needs
interactive printer validation.

The next printer run identified `unexpected_message` as the reconnect reason.
The client now ignores unrelated vendor reports/replies instead of closing the
stream, counted by `ignored_messages`. They do not refresh cache freshness or
modify fields. Subscription/heartbeat errors and malformed telemetry still
close the connection. The exact unsolicited printer message has not been
captured; hardware testing must confirm this addresses the reconnects.
