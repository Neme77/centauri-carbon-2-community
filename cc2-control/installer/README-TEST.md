# CC2 Control 1.2.21 — cumulative corrective trial

For Community Firmware V4.4/V4.4.1 and the V4.4.2 candidate, ELEGOO 02.01.01.07.

Fixes updater downloads when GitHub security headers exceed 8 KiB, preserving
the existing 16 KiB total-header budget and all certificate/redirect checks.
Older versions need this first signed panel upload from the computer; subsequent
downloads use the corrected helper.

Recognizes and retains a qualified dormant R2 camera directory left after
rollback or firmware reinstallation, authenticating its files and startup
fingerprints before reinstalling. Unknown components and unrelated startup
changes remain refused. The included camera R2 libraries are unchanged.

Install only while connected, idle and cold, with heater targets zero. A complete
idle-printer reboot is required on first camera-fix activation; restarting only
CC2 Control is insufficient. No printer reboot is automatic. Keep the exact
installer backup path for restoration.

Includes accepted stable Canvas, spool, object-map, file and UI improvements.
Canvas UDS, Quiet and extended pull-back remain separate experiments. Settings,
spools and plate libraries are retained.

The full firmware builder is separate: generate V4.4.2 on the maintainer's PC
and validate that complete image on hardware before publishing it.

Thanks to tharje for camera diagnosis, DamiBFryta, our ultimate beta tester, for
persistent testing and feedback, Surfoo, Nick Plateringen-Paul and all original
contributors whose credits remain in project history.

