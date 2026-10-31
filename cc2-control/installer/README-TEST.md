# Cumulative application trial: PR252 + PR253

CC2 Control remains version 1.2.23. Includes Surfoo's corrected Z arrows and the calibration-saving text corrections. Arrow controls remain icon-only; existing positive/negative accessible names are retained. No new heating, motion limits or calibration commands.

Extract the full ZIP and run Install-Windows.cmd, or the Linux/macOS installer. This is an SSH trial, not a signed panel upload. Use a connected, Idle, cold printer with heater targets zero and pause video. It updates only CC2 Control and preserves the already installed camera component; no complete printer reboot is required. Keep the printed backup/restore path. Reload with Ctrl+F5 after installation because the app version stays 1.2.23.

Test with the bed clear, axes homed and room for travel: select 0.1 mm first. The upper Z arrow should raise the bed (negative Z), the lower arrow should lower it (positive Z). Homing/Idle and backend movement limits are unchanged. Check the Input Shaper automatic-saving explanation and the explicit Save PID instruction; no need to rerun calibrations just to read the updated text. No physical-printer movement was performed by build automation. Report actual results before merge or release.
