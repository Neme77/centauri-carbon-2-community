# Print speed and flow controls

Dashboard and Job expose the effective speed and extrusion multipliers, manual
whole-percent inputs, and a separate Reset 100% button for each control. Live
movement in mm/s is shown separately from the speed multiplier. All labels ship
in English, Italian, French and Simplified Chinese.

Speed accepts 25–200%; flow accepts 50–150%. These are application limits,
not a guarantee that every value suits every material or print. Controls require
an active or paused job, a connected/registered MQTT session with telemetry no
older than 15 seconds, and fresh UDS readback for the selected multiplier.
The backend strictly rejects nonfinite, fractional, trailing or injected values.
Commands use the existing protected control worker: M220 for speed, M221 for
flow. The expert console lock remains unchanged.

`/api/printer` includes nullable `tuning.speed_percent`, `flow_percent` and
`live_velocity`. They use the existing UDS cache, so the UI adds no periodic HTTP
request. Missing/stale values render unavailable instead of assuming 100%.
The UI preserves edited inputs during polling, prevents concurrent tuning
submissions, and waits for a subsequent matching readback. If the value is not
confirmed within eight seconds it reports that fact and does not retry
automatically. Command acceptance does not guarantee execution. Display modes
and G-code can override these machine values; a manual speed adjustment need
not change the display's mode label. No preference persistence or automatic
reset on completion is added.

## Portrait phones

Only narrow portrait viewports (up to 767 CSS pixels wide) replace the fixed
sidebar with an expandable navigation drawer, a compact product title and
full-width content. The drawer closes on navigation, Escape, backdrop click or
rotation out of narrow portrait. Keyboard focus stays within the menu while
open. Form fields use 16px text in portrait to avoid input-focus zoom on mobile
browsers. Existing landscape and desktop navigation breakpoints are preserved. In portrait,
file-list names use a compact single line with ellipsis. Selecting a row reveals
its full filename, also retained in the detail panel and button title. Landscape
filename wrapping is unchanged.

## Validation

Run backend checks with `make clean test CROSS= CC=gcc` in `cc2-control`.
Run UI `npm run check` and `npm run build` in `cc2-control/web-src`.
The C harness tests strict bounds, command construction, active/paused state,
freshness, disconnected state and the HTTP UDS-readback gate.

An optional Chromium test uses the actual built page with simulated API
responses and records submitted actions. Install Playwright in a temporary
Node project, install its Chromium browser, then run:

```sh
CC2_PLAYWRIGHT_MODULE=/absolute/path/to/node_modules/playwright/index.mjs \
node cc2-control/tests/test_print_tuning_browser.mjs
```

If needed, set `CC2_BROWSER_PATH` to an existing Chromium executable.
`CC2_SCREENSHOTS` optionally points to an existing output directory. The test
covers Enter/Apply/Reset, readback, duplicate prevention, unavailable/idle states,
input preservation during refresh, portrait drawer navigation and page widths
320/360/390, plus landscape navigation at 740 and 844 pixels.

Real-printer validation of this new UI/API is still required: speed 80→100%,
flow 95→100%, changes during pause, display readback, and portrait/landscape on
an actual phone. The prior UDS protocol probes validated M220/M221; they do not
replace testing these new controls. MQTT workload reduction is a separate step.
