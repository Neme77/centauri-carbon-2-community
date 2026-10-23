# CC2 Control 1.2.20 — experimental cumulative update

This is a public prerelease for Community Firmware V4.4/V4.4.1, ELEGOO base 02.01.01.07. Stable remains 1.2.19. It is one application package, not a collection of downloadable modules.

Choose Experimental in Settings → Updates, read the notes, enter the LAN code, download/verify and separately confirm installation. Older stable interfaces call this channel experimental Canvas UDS. Users of the preceding stable-only trial can upload the signed .cc2update from the same page once. The new selector then remains available. Channel selection never enables a feature. The bell continues to report stable releases. Returning to an older stable app requires the installer backup; selecting Stable does not downgrade.

Canvas UDS and Quiet controls are separate machine-persisted opt-ins and default to off on a new installation. Existing choices are retained. Canvas UDS, slot states and Orca synchronization were accepted by the maintainer on 23 October 2026. Quiet/Normal on V4.4 still needs hardware qualification: enabling its controls sends no command, but actually selecting Quiet changes the motor mode. Slicer commands can override its initial 80 mm/s and 2000 mm/s² limits. Normal restores fixed 500/20000 limits; displayed mode is requested state, not verified TMC registers. Explicitly select Normal and wait for successful completion before disabling Quiet, changing Canvas source or updating the app while Quiet controls are enabled.

This cumulative package includes the unchanged qualified R2 camera libraries, including for users who skipped 1.2.19. A FULL IDLE-PRINTER REBOOT IS REQUIRED WHEN FIRST ACTIVATING THE CAMERA FIX. Reloading the page or restarting only CC2 Control is insufficient. A recognized existing camera component is preserved. No printer reboot is started automatically.

Install only while connected, idle and cold (nozzle/bed at most 50 °C, heating targets zero). Close live views first. Settings, materials, plates/nozzles and spools are retained. Keep the exact backup/restore path reported by the installer. The Windows/macOS/Linux installer is also included; use only this printer’s own backup.

Pull-back above 350 mm remains a separate firmware experiment. This update installs no extended runtime and does not enable it. An independently installed extended runtime requires its own qualified rollback, including effective settings at or below 350 mm before restoring the original printer executable. Builder and full-firmware downloads are unchanged.

Credits: Surfoo for UI fixes and Canvas/documentation ideas; BorjaLL for Quiet; Noahwhite2014 for extended pull-back; Nick Plateringen-Paul for the original object map; DamiBFryta, our ultimate beta tester, for suggestions and testing; tharje for camera diagnosis and recovery work. Original contribution histories and earlier credits are retained.
