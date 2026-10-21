# Combined CC2 Control trial — PR #190 and #196

Traditional application-only update. The trial keeps version 1.2.19 and is not a new release. It combines explicit Canvas recovery (#190) with Surfoo's UI/accessibility refinements (#196).

The installed camera patch is preserved: it is not bundled, reinstalled or reactivated. Preferences, plate and spool libraries remain in place. Installation restarts only CC2 Control; a printer restart is used below to reproduce the Canvas condition.

Install while connected, idle and cold (nozzle/bed at most 50 °C, both targets zero). Close live views. Upload the signed .cc2update in Settings → Updates, or use the Windows installer in the ZIP. Keep the printed backup path and refresh the page afterwards.

## Checks

1. Expand/collapse the sidebar: icons and the toggle should stay aligned. Check Files, Console, Spools and normal dialogs on desktop and mobile. Without an available update the bell opens Updates; with a notification, check the entire popup is visible.
2. Complete a normal Canvas load, leave the filament loaded, and normally restart the idle printer, following the already-observed reproduction procedure.
3. If head presence is reported with no assigned slot, select any present slot. Recover by loading slot N asks for confirmation: this is a real native load and may heat the nozzle and purge. Use Canvas filament after checking its path.
4. Cancel sends no load. After confirming, observe native loading, coherent final readback and restored normal actions. Delivery alone never means recovery completed.
5. Check the existing local/Matrix video and preferences, then report results before merging. Do not automatically repeat a failed load.

Use the installer's traditional rollback while idle and cold if needed. This trial does not change the public update channel.
