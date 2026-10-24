# Experimental bed heating for filament drying

Issue: #198. Requested by **romeopuch**. This is an opt-in pilot for the next
application release; it is not enabled or published in the stable catalogue.

Enable **Bed heating for filament drying** in Settings → Updates → Experimental
features. The Control page then offers a bed setpoint (30–90 °C), finite hold
(1–720 minutes), Start and Stop, protected by LAN confirmation. The timer begins
when fresh bed readback reaches the setpoint within 1 °C. It continues if the
page closes. It never resumes heating after a CC2/printer restart. There are no
material presets or claims that bed temperature equals filament/air temperature.

## Native heater lease

The supplied G-code uses six hour-long G4 waits. This implementation sends no
G4/M190, nozzle, movement, fan or stepper command and changes no vendor file.
It checks `gcode/help` for `SET_HEATER_BED_IDLE_TIMEOUT` and `M140` before heating.
It reads the configured bed idle timeout from printer.cfg/autosave.cfg, records a
private recovery marker, arms a **90-second native bed timeout**, waits for the
correlated reply, and only then requests the bed target once. While it owns the
bed with fresh idle MQTT/UDS evidence it renews only that short timeout every
30 seconds. It never reasserts a target changed by another user.

This uses the vendor bed timeout, **not upstream SET_IDLE_TIMEOUT** (which the
vendor uses for the extruder). Source reference:
https://github.com/elegooofficial/CentauriCarbon2/blob/main/elegoo/extras/idle_timeout.cpp
Published source is evidence for the design, not validation of the shipped
binary's timing. Native heater protection remains enabled. A stalled CC2 stops
renewing the lease; an actual printer test must establish the resulting cutoff.
The vendor timer depends on native idle/motion state; 90 seconds is a configured
lease, not a certified wall-clock cutoff under arbitrary motion or other code.

Stop/expiry requests M140 S0 and waits for fresh zero target readback before
restoring the configured idle timeout and reporting completion. A changed
nonzero target or a print begun elsewhere revokes heating ownership; no cooldown
is sent to that print. Restoration waits for idle and target zero. CC2 mutations
are blocked during the session except the drying Stop and emergency stop.

After a CC2 crash/restart, the marker blocks another session, no positive target
is sent, and restoration waits for idle/zero. A restart does not restore an old
hold deadline. Unknown/corrupt storage refuses heating. The configured timeout
is restored, not an unobservable custom timeout set earlier via raw console.
Current settings migrate schema 1 → 2 while preserving UDS/Quiet choices and
leaving bed heating off. Downgrading to an older app that cannot read schema 2
opts experiments out; back up settings before testing a downgrade.

## Required supervised hardware pilot (not yet performed)

1. Idle/cold printer, no ongoing calibration or transfers: use 40 °C / 1 minute.
2. Confirm Stop during heating and hold, zero readback, and configured timeout
   restoration; repeat with normal expiry. Confirm no axis/nozzle/fan changes.
3. Close/reopen the browser; confirm the same session and no timer extension.
4. Change the bed target/cool down from the display. Confirm no reassertion.
5. At idle only, stop CC2 during a short heating session. Observe the native
   heater timeout independently, then restart CC2: no heating resumption.
6. Validate telemetry loss and printer restart; do not induce a fault during a
   real print. Confirm an externally started print keeps its own bed target.
7. Only after these pass, try a longer supervised hold with a suitable spool.

Host/browser tests simulate these transitions; they do not qualify native heater
cutoff timing or drying performance. No printer has been heated by development.
