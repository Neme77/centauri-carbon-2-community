# External feed with Canvas connected (experimental)

This opt-in uses native USED_CANVAS_DEV and M600 commands. It does not disconnect Canvas electrically, suspend a service, patch the vendor binary, change firmware configuration, or replace the existing camera fix.

Enable **External spool with Canvas connected** under Settings → Integrations → Experimental integrations with an idle, cold printer and LAN-code confirmation. In Canvas or Control, the External spool card provides a Canvas / External spool source selector. A source change requires confirmed fresh native Idle state, no active Canvas channel, and no filament at the printhead sensor. Selection alone does not move or heat anything; the native command must acknowledge before the preference is saved.

Use a direct filament path and an appropriate material profile. Material changes address the external profile, never slots 1–4. Native external loading uses the profile maximum nozzle temperature. Loading/unloading reasserts external mode immediately before the native M600 procedure. Native procedures home/move, heat, purge and may cut/retract; the unload template also contains a Canvas rocker command. Remove printed parts before homing/loading and watch the first trial. These are native material routines, not newly tuned TPU speed profiles.

The UI choice is a persistent preference for the next operation, not a claim that the physical Canvas is absent or that the firmware will retain the mode after a print. The card remains visible and editable while Idle after completion. The firmware can unload and restore Canvas mode at print end. Unload is disabled when the connected-mode sensor says the head is already empty. The print setup defaults to external for a selected external single-colour job and still asks for confirmation; multi-colour jobs require Canvas. The existing acknowledged Canvas colour-table clearing before external starts is preserved.

For return: remove the external filament, select Canvas and load a native slot normally. Canvas actions are blocked while the external preference is selected. Before disabling the experiment, rolling back, or updating firmware, return to Canvas with the printhead empty. The separate /opt/usr/cc2-external-feed.json settings file preserves older app experiment settings; older applications ignore this file.

## Validation status

Maintainer hardware test: connected Canvas, direct PLA at 230 °C; native external load and purge, completed single-colour print, removal and return to normal Canvas loading passed. Manual unload with filament still loaded was not established: the printer had already unloaded at print end. TPU 95A is the next tester validation; PLA success does not establish TPU behaviour. Capture diagnostics and describe loading, motion, extrusion, errors, end-of-print state, and return to Canvas. Never repeat a load/unload command while it is running.

The cumulative trial also includes PR #241 (nonmotion Console classification) and PR #242 (completion notice path matching). These are trial changes, not a published release or an automatic-update candidate. Report the exact package hash and retain the installer backup path. No full printer reboot is required for the application-only trial.
