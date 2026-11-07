# Unified CC2 Control 1.2.26 candidate

This candidate uses one installer for a qualified first installation from Community Firmware 4.4.3, an active managed RFID upgrade and a verified already-current installation. Selection is automatic; an unknown or unfinished native state is preserved and refused rather than guessed. Windows/Linux/macOS launchers and signed app updates consume the same installer payload.

Includes corrected PR301 and the spool popup fix PR303, approved Control/Canvas graphics, Pressure Advance, 350 C nozzle controls, flap, EXHAUST minimum, load-cell tools, calibration and autonomous Canvas RFID activation/recovery. Qualified camera/network runtimes remain included. The original native display and LAN drivers are not replaced by this application package.

For an instructed hardware trial use an idle, cold printer, zero heater targets and no RFID scan or calibration, and close camera viewers. Keep the printed backup path and logs. Follow complete-printer reboot prompts from the Canvas RFID panel; do not reboot during transfer. Already active same-profile updates preserve the Canvas application and loaded guard. Unknown/partial states retain their backups.

This is a qualification candidate, not a public release. The build itself accesses no printer or physical serial port. Check the corresponding qualification report for actual simulation and hardware acceptance; protocol acknowledgements are not physical flash readback.
