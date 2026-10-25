# Cumulative PR 218, 219, 220, 222 and 223 trial — R1

Maintainer-only validation package, not a public release. Version stays 1.2.21.
Install from the computer using Install-Windows.cmd / Install-CC2-Control.ps1;
the in-app updater rejects the same version. Keep the printed backup/restore path.
Install only while connected, cold and idle, with heater targets zero.

Includes the approved Dashboard camera artwork, header Keep active preference,
draft speed/flow sliders plus existing numeric Apply/Reset, experimental features
under Integrations, opt-in timed bed heating, completed-print notice/idle progress,
compact filtered spool history and V4.4.2 updater requirements in all languages.
Existing preferences and the already installed webcam patch are retained; this
application-only trial does not reinstall the camera component or reboot.

Check ordinary UI, live-camera navigation, printing sliders, completion notice,
idle progress, spool filters and settings placement first. Enabling bed drying
must not heat anything. Before a short supervised empty-bed heating/stop test,
read cc2-control/docs/BED_DRYING.md in the included sources; verify target-zero
readback before leaving the printer. Do not begin with an unattended drying cycle.

Credits: pezso92 (visual direction/artwork), romeopuch (drying request), DamiBFryta
(spool feedback/testing), and contributors credited in the individual PRs.
No public release, stable catalogue or complete firmware is published by this trial.
