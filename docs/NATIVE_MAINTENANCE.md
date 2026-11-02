# Native maintenance tools

The Control page adds explicit, on-demand controls beside the existing EXHAUST
fan and beneath the load-cell diagnostics. Opening the page does not send a
hardware command. Existing card organization is retained.

## Exhaust grille

Open and Close send the native `EXHAUST_GRILLE_OPEN` and
`EXHAUST_GRILLE_CLOSE` commands. The native firmware owns the mechanics and may
also regulate the exhaust fan. These actions require fresh Idle telemetry,
native standby/complete and zero heater targets, with no conflicting procedure.
Do not infer flap position from the last submitted action: the panel reports
command acceptance, not a verified position sensor reading.

## Extruder Rotation Distance

Read obtains the active value from a dedicated native connection; the configured
value is shown separately. The parser accepts the exact deployed `Extruder ''`
and `Extruder 'extruder'` reports, not arbitrary tool names or guessed values.
Apply checks that the active value has not changed since it was read. Restore
uses the configured reference, including after a process/printer restart, rather
than a lost in-memory previous value.

Save changes only the extruder `rotation_distance` autosave entry, retaining
other settings, an original backup and a boot-scoped pending record. Conflicting
native settings, another calibration or externally changed files prevent saving.
The protected restart requires an idle printer and intact saved contents.
Applying a value does not automatically save or restart anything.

## Load cell

Read and Diagnose return the native output. Tare is separate from the guided
calibration. The guide requires an explicitly entered, confirmed known mass,
an unloaded stable sensor for zeroing, and the manufacturer's intended load
point. It does not move the head or switch on heaters. Do not press on the nozzle
or improvise an unknown loading point.

The owned native interactive session is tracked through tare, measure, accept,
save, cancellation and restart. `ACCEPT` is refused for incomplete results.
Save validates only the expected cell settings and refuses unrelated native
pending changes. Mass limits derive from the configured cell safety limit.
Calibration mass starts blank and the submitted mass remains visible.

## API and concurrency

- `GET /api/calibration-tools/status` reads the latest local tool state.
- `POST /api/calibration-tools/action` uses the same origin/authentication policy
  as other control actions. Plain action identifiers include `rotation:read`,
  `rotation:apply:<expected>:<target>`, `rotation:save:<expected>`,
  `rotation:restart`, `cell:read`, `cell:diagnostic`, `cell:tare`, `cell:start`,
  `cell:zero`, `cell:measure:<grams>`, `cell:accept`, `cell:abort`, `cell:save`,
  `cell:restart`, and the protected discard action.
- Native tools use a bounded nonblocking transport. The loop observes printer
  state and service ownership while work is pending. Hardware commands are not
  sent by a status read or a background page-open operation.
- Nozzle-expansion, bed/chamber heating, plate calibration, PID, pullback saves,
  pending settings and app updates retain their conflict guards. An owned
  load-cell session also blocks unrelated mutations. Emergency stop remains
  available independently.

## Validation record

The owner physically tested grille open/close, Rotation Distance read/apply/save
and reboot, load-cell read/diagnostics/tare and the complete guided save/reboot
cycle in the preceding private build. A known approximately 100 g load returned
approximately -99.6 g, then approximately zero when removed. This is a functional
check, not a traceable accuracy certification or a recommended calibration value
for another printer.

Actual browser controls have passed interaction, feedback, confirmation,
responsive layout and post-reboot restore checks. The merged cumulative source
still requires its separate host/ARM/sanitizer qualification and physical
cumulative acceptance. Synthetic tests never heat, move or contact a printer.
