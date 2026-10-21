# CC2 Control UX roadmap mockups

Design-only proposal for [#178](https://github.com/Neme77/centauri-carbon-2-community/issues/178). Open the [interactive mockup](./mockups.html) to switch between Dashboard and Canvas, and desktop and mobile previews.

All displayed values are illustrative. The preview uses no external assets, printer commands, API calls, or additional polling.

## Dashboard

- Emphasizes current job state, progress, remaining and elapsed time, and layer count.
- Groups nozzle and bed actual/target temperatures, fan percentage, connection, and camera state.
- Shows print and camera actions as layout placeholders; implementation should retain existing behavior and safety guards.

## Canvas

- Shows four slots and active/head association only as reported by the printer.
- Makes unavailable or ambiguous data visibly **Unknown**.
- Does not imply that telemetry confirms physical filament movement, purge completion, or a successful unload.

## Review before implementation

- Does the information hierarchy work on desktop and mobile?
- Is the four-slot view an accurate, useful way to show reported and unknown states?
- Should any details move before implementation is scoped?

This proposal does not change the production UI. Keep the existing layout as the default; consider accessibility and visual consistency in small, focused changes. The optional density setting and diagnostics hub remain separate future work.
