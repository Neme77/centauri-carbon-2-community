# CC2 Control UX roadmap mockups

Design-only proposal for [#178](https://github.com/Neme77/centauri-carbon-2-community/issues/178).

- [CC2-style Dashboard pass](./dashboard-style-pass.html) — a focused refinement of the current Dashboard shell, with desktop/mobile and dark/light previews.
- [Dashboard and Canvas concept](./mockups.html) — the broader information-hierarchy proposal for desktop and mobile.

The new Dashboard pass follows the current CC2 identity, familiar navigation and existing page structure while refining spacing, typography and hierarchy. All displayed values are illustrative; controls are visual placeholders with no printer behavior.

## Dashboard and Canvas concept

- Emphasizes current job state, progress, remaining and elapsed time, and layer count.
- Groups nozzle and bed actual/target temperatures, fan percentage, connection, and camera state.
- Shows print and camera actions as layout placeholders; implementation should retain existing behavior and safety guards.
- Shows four Canvas slots and active/head association only as reported by the printer. Unavailable or ambiguous data stays **Unknown**.
- Does not imply that telemetry confirms physical filament movement, purge completion, or a successful unload.

## Review before implementation

- Does the familiar Dashboard hierarchy feel clearer on desktop and mobile?
- Does the revised spacing and typography fit the existing CC2 identity?
- Are any details unclear at narrow widths or with longer translated labels?

This proposal does not change production UI. Keep the current layout and navigation familiar; review visual direction before choosing small, focused implementation PRs.