# Remembering the LAN code on a personal device

Settings → Connection offers **Remember the LAN code on this device**, off by default.
Enter the printer LAN code in its dialog and choose **Verify and remember**. Verification
uses the existing rate-limited LAN comparison and same-origin request protection; it
returns only a boolean, sends no printer command and does not change setup credentials.

The browser stores the entered code in localStorage for the exact origin (scheme,
printer address and port). It is not encrypted by CC2 Control. Anyone using that browser
profile can authorize protected actions. Use this option only on a personal device.
A different address, browser profile or device needs its own opt-in.

Updates and experimental controls (including bed drying) prefill masked code fields.
Action confirmations, cold/idle eligibility, native heater guards, package signatures
and all server-side authorization remain in place. No action runs on opt-in or refresh.
A rejected saved code is forgotten. Connection credential revalidation also clears the
browser copy. **Forget code** or turning the switch off removes it, synchronizing other
open tabs. Browser storage restrictions leave the option unavailable instead of claiming
that a code was saved.

The stored value is not added to printer preferences, diagnostics, configuration backups,
URLs or catalogue requests. The printer's existing MQTT code is never returned to the
browser. Backup/restore keeps its separate explicit confirmation and does not reuse
this convenience option.
