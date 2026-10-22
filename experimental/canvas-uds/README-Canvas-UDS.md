# Canvas UDS — opt-in experimental port

This ports the earlier local CC2 Control 1.2.4 Canvas-UDS prototype onto the
current application. It is independent of #172 (extended pull-back) and #179
(Quiet motors). Full filament ejection (#103/#114) remains excluded.

The normal startup is unchanged. The expanded subscription is selected only
when starting CC2 Control with `--experimental-canvas-uds`. No persistent startup
file is changed by this PR, no release number is consumed, and no catalogue or
signed tester package is published.

## Behavior

- Subscribe to the complete Canvas Lite state through the existing UDS
  connection: type, device, active channel, refill and four tray records.
- Keep a fixed-size typed cache, merge partial deltas and normalize colors,
  material strings and nozzle temperatures into the existing Canvas JSON shape.
- Use the complete fresh cache for `/api/canvas`, Orca lane metadata and
  slot eligibility. Report `source: uds` or `source: mqtt` on `/api/canvas`.
- Preserve the real MQTT snapshot separately. An incomplete, malformed,
  unsupported, disconnected or stale UDS stream returns to the MQTT view.
- Preserve native commands, connection/freshness/idle/head guards and the
  explicit recovery action introduced in #190. No new feeder motion, retries,
  UDS query loop or GUI inference is introduced.
- Keep existing bounded MQTT discovery/refresh and spool metadata accounting
  unchanged in this first port. In particular, this candidate does **not** yet
  suppress MQTT Canvas refreshes or move spool metadata to a new source.
  Do not claim a measured CPU/RAM improvement.
- `/api/uds` exposes `canvas_uds_enabled` and `canvas_uds_available` separately.

## Validation and printer qualification

`test_canvas_uds.c` exercises the actual subscription transport, captured initial
snapshot, deltas, units, ordering, API rendering, Orca lanes, normal/recovery
eligibility, stale/disconnected fallback and explicit opt-out. Existing native,
ARM, sanitizers and UI checks must pass on the final source snapshot.

No hardware validation of this port is claimed. A tester package still needs a
reviewed opt-in launcher and restore path that retain the camera patch and the
other installed components. Before distribution, verify the enabled source
and fallback, all four tray states, reboot recovery, native load/unload,
material updates and Orca synchronization on V4.4/V4.4.1. Observe a multicolour
print separately, including spool attribution. Test one experimental feature
at a time initially. Neither an accepted command nor a CI result proves
physical loading or unloading.

Read-only checks after a properly prepared trial installation:

```sh
wget -qO- http://127.0.0.1:8081/api/uds; echo
wget -qO- http://127.0.0.1:8081/api/canvas; echo
wget -qO- 'http://127.0.0.1:8081/server/database/item?namespace=lane_data'; echo
```

Original local prototype: CC2 Control Canvas-UDS test 1.2.4 (13 October 2026).
This is an application-source port; vendor firmware and private inputs are not
included. Existing contributor attribution remains intact.
