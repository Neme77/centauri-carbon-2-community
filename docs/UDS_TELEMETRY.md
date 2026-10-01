# Local UDS telemetry

CC2 Control subscribes to `/tmp/elegoo_uds` using `objects/subscribe`. The
subscription is read-only and uses the existing HTTP/MQTT event loop. No G-code
is sent by this telemetry client. The console keeps its independent connection.

The initial response and subsequent `cc2_status` notifications populate a bounded
cache. Notifications contain changed fields only. Initial responses arriving
after a newer notification fill missing fields without replacing newer values.
The client handles split/coalesced ETX-delimited frames, disconnects on oversized
or invalid frames, and retries connections at most once every five seconds.
A small read-only `info` heartbeat every two seconds detects a silent dead peer.

`GET /api/uds` exposes connection/freshness status and nullable values, including
speed/flow factors, live velocity, fan fractions/RPM, progress and elapsed times.
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
