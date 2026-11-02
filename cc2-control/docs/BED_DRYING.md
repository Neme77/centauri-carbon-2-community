# Experimental bed heating for filament drying

Issue: #198. Requested by **romeopuch**. This is an opt-in experiment included in CC2 Control 1.2.22. It starts disabled on a fresh installation.

Enable **Bed heating for filament drying** in Settings → Integrations → Experimental
integrations. The Control page then offers a bed setpoint (30–90 °C), finite hold
(1–720 minutes), Start and Stop, protected by LAN confirmation. The timer begins
when fresh bed readback reaches the setpoint within 1 °C. It continues if the
page closes. It never resumes heating after a CC2/printer restart. There are no
material presets or claims that bed temperature equals filament/air temperature.

## Native heater lease

The supplied G-code uses six hour-long G4 waits. This implementation sends no
G4/M190, nozzle, movement, fan or stepper command and changes no vendor file.
`gcode/help` is checked only for `SET_HEATER_BED_IDLE_TIMEOUT`, the native safety lease. The vendor response can omit registered M-codes such as `M140`; the app verifies that command separately through its correlated script reply and fresh bed-target readback. The regression fixture follows the printer's help-response shape with no M-code entries.
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

## Validation and additional supervised checks

Neme77 tested heating at 50 °C / 2 minutes, manual Stop and normal expiry with a confirmed zero bed target on the printer. These checks do not establish spool drying performance or the native cutoff timing under every fault condition. The remaining checks below describe additional qualification scenarios, not a claim that all have been physically tested.

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
cutoff timing or drying performance. Development commands do not heat a printer; the supervised hardware checks above were performed separately by the maintainer.

## Chamber preheat

Issue: #270. Requested by **Sceletor**; specified by **Noahwhite2014**, reviewed by **Neme77**.
The same experiment offers **Chamber preheat** on the Control page: the bed heats the empty
chamber before an ABS/ASA-type print. It is the drying session above with these differences:

- **Start:** a chamber target (30–55 °C), a bed setpoint (60–100 °C, and never above the printer's
  own `[heater_bed] max_temp`) and how long to hold once ready (1–240 minutes). The bed may still be
  warm from a print; its target must be zero. `gcode/help` must also list
  `SET_CAVITY_TEMPERATURE_MODE`. The vendor heater check (`[verify_heater heater_bed]`:
  `heating_gain=2`, `check_gain_time=60`, `hysteresis=5`) wants 2 °C per minute until the bed is
  within 5 °C of its target. On a test printer the bed gained about that much near 99 °C, and a
  105 °C target stopped with error 101 ("not heating at expected rate"); 100 °C clears the check
  at 95 °C.
- **Arming:** with the 90-second lease it closes the exhaust grille (`SET_CAVITY_TEMPERATURE_MODE
  MODE=1`, the touchscreen's chamber hold) and sets the aux fan to 20 % (`M106 P2 S51`). The grille
  servo checks its end switch every 5 s and reports a close failure (1103) after five misses. On the
  test printer that warning came from a dislodged end switch while the flaps closed normally.
- **Heating:** the aux fan stays at 20 % while the chamber is 5 °C or more short of the target and
  then tapers linearly to off. The chamber sensor reads whole degrees, so once it has stayed
  within 1 °C of the target for two minutes the chamber is **ready**: the page and a popup on every
  page say so, and the hold starts. The bed's own target is never changed. Two hours without
  becoming ready, or a lost chamber reading, stops the session.
- **Levelling off:** a minute-by-minute chamber log is fitted to a first-order rise over the last
  20 minutes (from 10 minutes in). When the estimated level is more than 1 °C short of the target
  the page shows it, so the user can lower the target or stop. Once the chamber has risen less
  than 1 °C in 20 minutes it is **levelled off**: the hold starts there and the page and popup say
  the chamber is as warm as the bed can make it, rather than keeping the bed at temperature until
  the two-hour limit. On the test printer (bed 100 °C, target 50 °C) the chamber rose from 36 °C to
  46 °C in 20 minutes and 49 °C in 40, then stayed at 49 °C.
  With the bed alone the chamber usually levels off around 40–50 °C, lower in a cold room, which is
  why targets above 55 °C are refused until a chamber heater is supported.
- **Hand-off:** a print that starts while preheating takes the bed over. Nothing is sent to it: no
  `M140 S0`, and the grille stays closed for the print. The vendor bed idle timer does not run
  while printing.
- **Restore:** stop, expiry, a lost chamber reading, the time limit, and the end of a print that
  took over all finish the same way once the printer is idle with a zero bed target: the configured
  bed idle timeout, the grille state and the aux fan speed recorded at the start are restored
  (grille first, since opening it can set the aux fan). The recovery marker
  (`CC2DRY2 timeout bed grille fan`) carries those values, so a CC2 restart restores them too and
  never resumes heating.

Still to be measured on a printer: the levels reached at different room temperatures and bed
setpoints (Neme77 offered to log ambient, bed target and time to 45 °C), and the full hand-off
with a real print.
