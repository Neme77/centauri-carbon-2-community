# Native filament controls

The Control page shows an External spool card when a fresh native readback confirms
`canvas_dev.canvas_lite.connected == 0`. A failed/unknown transport is never treated
as a disconnected Canvas. Load and Unload are fixed native `M600 S1` / `M600 S2`
procedures, as used by the stock display. Every protected external action re-queries
native connection, active channel and idle print state before command construction;
MQTT must independently report fresh registered Idle state. Existing console
serialization prevents a second command while the procedure is running.

Canvas and the external spool share Brand / Type / Subcategory / Colour editing.
The catalog contains 84 stock display combinations for ELEGOO and Generic, with
their native temperature ranges and codes, and the 24 colours reported by the
printer. It contains factual data, not vendor executable code. Provenance: stock
02.01.01.07 GUI SHA256
`24d0e561287a4b5a87c8ec698c98f60acf2c70511114b36224b4eb9249283abd`.
Entries above the existing protected 320 C metadata range remain visible but cannot
be applied; ranges are not silently clamped. The browser and backend catalogs have
a parity test. Material values sent to firmware are backend-owned catalog entries,
not browser-supplied G-code or arbitrary temperatures.

This does not implement external feeding while Canvas is physically connected.
Native filament procedures can heat, home, purge and move the printer. They require
operator confirmation; no hardware commands are sent by opening a page.

## Validation

- Real printer: disconnected Canvas MQTT sample reported `connected:0`, with four
  empty placeholder trays (all tray_id=0). User confirmed display edit fields.
- Offline stock GUI/backend analysis identified external commands and material
  editing path (`CANVAS_SET_FILAMENT_INFO EXTERN_FILA=1`).
- Automated: native parser/action guards, catalog parity, actual browser edit/save,
  dependent dropdowns, palette, load/unload confirmations, stale/busy/present guards,
  desktop/mobile layout.
- Still required: native readback shape with Canvas absent, material round-trip on
  display, and supervised physical external load/unload on a candidate build.
