# Architecture

CC2 Control is a small static service designed for the resources available on
the Centauri Carbon 2.

## Runtime components

| Component | Responsibility |
|---|---|
| `cc2-control` | HTTP API, static UI, printer state and safety policy |
| MQTT client | Printer telemetry and supported control requests |
| Panda bridge | Compatibility endpoints on port 7125 |
| Web UI | Dashboard, files, Canvas, levelling, console and settings |
| procd service | Startup ordering, supervision and clean restart |

The backend is written in C and serves the single-page UI from
`cc2-control/web/index.html`. Persistent state belongs under
`/opt/usr/cc2-control`; immutable application files belong under
`/opt/inst/cc2-control`.

## Safety boundary

Routine operations use explicit backend actions. Arbitrary G-code remains
protected, dangerous operations require confirmation and printer-state guards
are enforced server-side rather than relying only on disabled UI controls.

## First-run setup

The LAN access code is stored only on the printer. After the first successful
registration, CC2 Control exits cleanly and procd restarts that service alone.
Printer firmware, Klipper, Canvas and the MQTT broker are not restarted.
