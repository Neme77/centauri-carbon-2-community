# CANVAS insertion during a print

The pinned printer executable records new inlet presence during a print. Its short prefeed path runs when the break counter exceeds the runout threshold. This trial adds an inlet grip for a newly inserted standby spool during steady printing. The resolved `pre_load_fila_dist_2` value must be 1–5 mm. The module uses that distance with the original asynchronous inlet motor command. The existing insertion event and tray status remain the source of web readback.

The trial requires a fresh CANVAS controller sample, the native settled feed flag, an active and loaded CANVAS tray, filament at the new inlet and no pending prefeed on that inlet. An insertion that occurs during a colour change is recorded as present and requires reinsertion after the feed settles to trigger the grip. The native code handles idle insertion and the stock runout prefeed. The hook uses the existing feeder call site and the CANVAS controller keeps ownership of slot state.

Build and run the ARM fixture with `make all test` in this directory. The fixture enters the actual ARM hook with the native stack layout and verifies the requested motor, its arguments, refusal cases and the stock runout branch. The library activates when preloaded by the printer launcher with the exact ELEGOO 02.01.01.07 executable installed at `/opt/usr/cc2-reactor-runtime-v2/elegoo_printer`. The constructor verifies the executable and the instructions before it changes the process image in memory.

The regular updater does not load this trial. Hardware qualification needs a test launch with the existing printer modules and this library, followed by a print from one CANVAS slot and insertion into a spare slot. Observe the grip, uninterrupted printing and the updated slot and spool assignment in CC2 Control. Include an idle insertion, a colour change and a runout in the qualification.

## Printer test

On 2026-11-18, a Centauri Carbon 2 running the pinned 02.01.01.07 base with `pre_load_fila_dist_2=5` printed from slot 1 while the operator inserted PLA Silk in empty slot 4. The operator confirmed that the feeder held the filament after release. The native log recorded two short-prefeed dispatch attempts 9.5 seconds apart during insertion. The cause of the second dispatch remains unverified. Slot 4 stayed present in the following readbacks over two minutes.

The running web interface opened the spool question and showed the assigned spool in slot 4 after confirmation. The print continued from slot 1, and its recorded extrusion increased while slot 4 recorded zero extrusion. This run exercised insertion into one standby slot during a single-colour print. Idle insertion, colour changes and runout remain separate hardware test cases.
