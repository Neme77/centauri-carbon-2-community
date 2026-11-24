# CANVAS insertion during a print

This trial adds a short inlet grip when filament enters an empty standby slot during steady CANVAS printing. It uses the original asynchronous feeder command with the resolved `pre_load_fila_dist_2` value, which must be 1–5 mm. The printer's existing insertion event and tray status drive the web readback.

The grip requires a fresh controller sample, settled feed from a loaded CANVAS slot, filament at the new inlet and no pending prefeed for that inlet. An insertion during a colour change is recorded as present. Reinsert into the spare inlet after the feed settles to check the grip. The stock idle and runout paths still handle their usual events.

## Qualified setup

Use an already provisioned Centauri Carbon 2 with the guarded RFID/Reactor/cavity-floor stack on **ELEGOO 02.01.01.07** and the qualified CC2 Control idle/telemetry probe. Universal 07 uses ELEGOO `02.01.01.13` and requires its own port and qualification. This is an explicit test launch outside the regular updater. The trial script checks the installed file hashes, guarded boot policy, active RFID lifecycle and loader manifest before switching. [`trial.sh`](trial.sh) contains the full path/hash allowlist, including the installed CC2 Control executable and startup helpers.

| File | SHA-256 |
| --- | --- |
| `/opt/usr/cc2-reactor-runtime-v2/elegoo_printer` | `19712b5943b10bb8cb6aab9a4ed498ee962b28538b23c91b7ed003eb9e590745` |
| `/opt/usr/cc2-reactor-runtime-v2/libcc2-reactor-v2.so` | `4639364c68489ec40c4e0c4f425550f3aa19e44c2ee61ed641affdc560e7e92c` |
| `/opt/usr/cc2-cavity-floor/libcc2-cavity-floor-r1.so` | `52d400605dd51f89eb95248c42a1661b3c0e60fadec5453d27a987729332fb10` |
| `/opt/usr/cc2-rfid/libcc2-rfid.so` | `fe6210eebfde592e4b0d276d2097abb0cfa3d53264da7ee9d83d8f24d7597ce2` |
| `/tmp/cc2-canvas-live-loading/libcc2-canvas-live-loading.so` | `b5b37346335dfb8b403f3465cdaecfc705769461322a1387d92793019f34af10` |

The three installed libraries above load in that order. The trial appends its library to that list. For the printer checks below, verify `pre_load_fila_dist_2=5` in the installed `[canvas_dev]` configuration and check for a later override in `/opt/usr/cfg/autosave.cfg`. Record the resolved setting with the results.

## Build and stage

On a Linux build host, use GNU Make, the ARM hard-float cross toolchain and `qemu-arm`. The hash below was reproduced with Ubuntu 26.04.1, `gcc-15-arm-linux-gnueabihf` `15.2.0-16ubuntu1cross1`, `binutils-arm-linux-gnueabihf` `2.46-3ubuntu2` and `libc6-dev-armhf-cross` `2.43-2ubuntu2cross1`. The QEMU fixture exercises the ARM hook, refusal cases and the stock runout branch.

```sh
cd experimental/canvas-live-loading &&
make all test &&
arm-linux-gnueabihf-strip --strip-all \
    -o build/libcc2-canvas-live-loading.trial.so build/libcc2-canvas-live-loading.so &&
printf '%s  %s\n' \
    b5b37346335dfb8b403f3465cdaecfc705769461322a1387d92793019f34af10 \
    build/libcc2-canvas-live-loading.trial.so | sha256sum -c -
```

With the hash verified, copy the two files using a trusted SSH connection and a new private staging directory. Replace `PRINTER_ADDRESS` with the test printer's address. `scp -O` uses the legacy transfer protocol supported by the printer.

```sh
printer=root@PRINTER_ADDRESS
stage=/tmp/cc2-canvas-live-loading
ssh "$printer" "mkdir -m 700 '$stage'" &&
scp -O trial.sh "$printer:$stage/trial.sh" &&
scp -O build/libcc2-canvas-live-loading.trial.so "$printer:$stage/libcc2-canvas-live-loading.so"
```

## Cold start and rollback

Finish any job and let the printer cool to Idle with both heater targets at zero. Keep automatic queue starts and other operators or clients from starting work during either transition. Confirm CANVAS is online and record the baseline tray states in the panel. On the printer, run these commands and stop at the first failure.

```sh
cd /tmp/cc2-canvas-live-loading
sh trial.sh check && sh trial.sh start && sh trial.sh status
```

A successful start reports `READY: trial native process, CANVAS connected, CC2 Control reconnected` and `mode trial`. The script checks the loaded library and the branch instruction in the native process, then waits for CANVAS and fresh panel telemetry. Confirm the panel reports Idle and compare the tray states with the baseline. The temporary launch log is `native-trial.log` in the staging directory.

After testing, bring the printer back to cold Idle. From the staging directory run the rollback.

```sh
sh trial.sh stop && sh trial.sh status
```

Expect `READY: stock native process, CANVAS connected, CC2 Control reconnected` and `mode stock`. This checks the original preload list and original branch instruction after running the standard printer launcher. Reload the panel and confirm CANVAS and tray readback again. Keep the logs until the result is recorded. A normal reboot in safe Idle starts through the original boot path and clears the temporary stage. If the script reports incomplete recovery or unknown mode, end the trial, keep the logs and verify stock startup before starting another job. Use the printer's normal reboot only once motion and heating are safely stopped.

## Grip qualification after review

Run the same test cases with `mode stock` and `mode trial` on the qualified setup. Record firmware and module hashes, CANVAS model/firmware, setting, job and slot map, the auto-refill setting, the resolved inlet `filter_count` and the capture time base. Keep native testing on this stack for the comparison. Exercise the PR panel's inventory safeguards with its own build and stock native startup.

### Evidence for the two dispatches

1. Start a timestamped passive capture of both directions of the CANVAS serial link, using a voltage-matched high-impedance analyzer on verified signals. Connect it with the printer powered off and leave the stock link in place. The qualified host uses `/dev/ttyS1` at 115200 baud. Keep the raw capture through at least 15 seconds after release or the last unexpected movement, whichever is later. Use a verified inlet sensor input as an additional analyzer channel if the status reply cadence cannot resolve short pulses.
2. Record the inlet, the operator's hand and marks on the filament against a fixed millimetre scale in the same video. Align the video, analyzer and host log with a common visible/captured marker and record clock offset/resolution. Note insertion, hand release, any deliberate removal and each motor start/stop. Record measured inward travel for each motor step, cumulative inward travel and net displacement. Mark the measurement inconclusive if hand movement and the motor step cannot be separated. The configured or transmitted distance is a command, while the ruler measures actual travel.
3. Collect the `exec automatic pre-feed process, channel` lines with the source location from `/opt/usr/logs/elegoo.log` and timestamped HTTP readbacks of `/api/printer`, `/api/uds` and `/api/canvas` for job state, active slot and tray readback. The panel reports cached data. Use the serial capture for inlet edges and command timing. A native log line marks an attempt at a call site shared with stock runout. Record the matching serial response, then use the per-slot activity/counter changes and video of the mechanism coming to rest to establish completion.
4. For each attempt record the last empty sample, the first present sample, any later falling and rising edge, the command and its reply, the following status/counter samples, and the position before and after motion. Compare each request's distance field with the values below and align the edges with the video. A visible withdrawal/reinsertion with matching edges establishes a new insertion. Edges with the filament held still point to intermittent detection. For a repeat with no observed edge, check for a controller state reset and record the maximum gap between samples. Leave the cause unresolved when that gap could hide an edge or completion. Compare time-aligned movement separately for both attempts.

For the pinned executable, the relevant short serial frame is `3d 00 LENGTH CRC8 PAYLOAD CRC16`. `LENGTH` is total frame size. CRCs are MSB-first with no final XOR. CRC8 uses polynomial `0x39` and initial `0x66` over the first three bytes. The final big-endian CRC16 uses polynomial `0x1021` and initial `0x913d` over all preceding bytes. Keep other frames in the raw capture.

| Direction and command | Fields in the payload |
| --- | --- |
| Controller to host, `0x7f` all-feeder status | Controller ID, command, common status, then four 9-byte slot records. Each record contains a status byte (bit 1 inlet present, bit 6 motor dragging, bit 0 fault), signed big-endian 32-bit odometer and motor encoder counts. The printer applies an inlet filter to these samples. |
| Host to controller, `0x92` single-feeder command | Controller ID, command, one-based slot, signed big-endian 16-bit distance, big-endian 16-bit speed and acceleration. Record the bytes and direction of the matching controller response. |

At the shared printing call site in the pinned executable, the trial sends the configured short distance in the `0x92` request. Stock runout sends that value plus 30, with a separate stop based on the odometer target or deadline. For the 5 mm setting, the outgoing distance fields are 5 for the trial and 35 for stock runout. Use the request to identify the branch and the synchronized capture to measure actual movement.

Use the existing serial traffic for this check. Opening the device from a second reader or repeatedly opening a direct UDS connection can disturb the test. Retain the original captures locally and attach only the observations and redacted excerpts to the review.

### Printer cases

| Scenario | Procedure and what to verify |
| --- | --- |
| Cold Idle insertion | With an empty slot and no active feed, insert once and release. Compare grip, status/readback and movement with the stock run. Remove at rest using the normal inlet release. |
| Steady single-colour print | Print from a known loaded slot, with another slot empty. Once the first slot is feeding steadily, insert once into the spare slot and release. In trial mode expect one short grip on that slot per distinct insertion, the filament held after release and the configured command distance in the trace. Confirm the original slot keeps feeding and the print progresses without pause or error. |
| Repeated insertion/removal | On the same spare inlet, repeat three complete cycles. Keep hands off through the observation window before each removal and wait for a fresh empty sample and a settled feeder before reinserting. Count edges, commands and actual motion for each cycle. The grip must stop after each short movement and stay quiet while the filament remains present. |
| During and after a colour change | Use two other loaded slots for a planned tool change. Insert into the spare inlet during the transition. Verify presence is reported, the spare inlet gets no trial grip during the transition, and the expected old/new feed handoff completes. After the new feed settles, remove and reinsert into the spare slot and verify the short grip there. |
| Runout | Use a short test job and a nearly exhausted active spool so it runs out normally. Keep the stock and trial comparison on the same refill configuration. Record the stock prefeed and the expected refill or pause/recovery. Confirm the trial follows that path and any spare-slot activity matches the expected runout action. |

For the panel check, confirm the empty-to-present slot prompts for the inserted spool and shows that assignment after confirmation. Check that extrusion continues on the actual feeding slot and a clear or replacement of its assignment is rejected. Stop the hardware test on unexpected motion, loss of the active feed, a jam or a fault. Record each case as passed, failed or inconclusive with the trace and measured travel.

## Printer test

On 2026-11-18, a Centauri Carbon 2 running the pinned 02.01.01.07 base with `pre_load_fila_dist_2=5` printed from slot 1 while the operator inserted PLA Silk in empty slot 4. The operator confirmed that the feeder held the filament after release. Two entries at the shared prefeed dispatch site were logged 9.5 seconds apart. The controller trace and actual total movement were not recorded, so the reason for the second entry remains unresolved. Slot 4 stayed present in the following readbacks over two minutes.

The running web interface opened the spool question and showed the assigned spool in slot 4 after confirmation. The print continued from slot 1, and its recorded extrusion increased while slot 4 recorded zero extrusion.

On 2026-11-19, the stripped module above matched the stripped library from the insertion test byte-for-byte. With the printer cold and Idle, `check`, `start`, `status`, `stop` and `status` passed on the printer. The checks confirmed the patched instruction and all four tray readbacks during the trial, the original preload list and instruction after rollback, and a fresh CC2 Control connection on both transitions. A deliberately altered staged library was rejected at `check` with the stock process still running. Repeat insertion/removal, idle insertion, colour changes, runout and the measured live trace remain open hardware cases.

### Dispatch investigation

The archived `elegoo.log` records report `mmu.cpp:2907`, thread `7873` and channel `3` (slot 4) at `02:27:36.958` and `02:27:46.454` on the printer clock (2026-11-19, UTC+08:00). The interval is 9.496 seconds. Both records follow the asynchronous feeder call on the printing path. The non-printing path emits the same text at line 2892. The surrounding panel samples show slot 4 going from empty to present and slot 1 still feeding. The saved trace has no raw inlet samples or panel readback between the calls. A filtered log query from 02:27:30 to 02:27:55 returned only these two CANVAS records. The break counter was not sampled.

In the fingerprinted executable, `Canvas::auto_prefeed_filament` checks for a present inlet and an empty tray at `0x005dfff0–0x005e003c` and sets the tray present at `0x005e0068` before dispatch. The second call required the host to see the tray as empty again after the first. In steady operation with the same controller object, the clear at `0x005e04d0` requires filtered inlet absence. The first controller status parse can also initialize tray state. The normal reconnect path retains that initialization state. The deadline path calls feeder stop at `0x005e0608`, then clears the deadline.

An offline QEMU replay of the pinned ARM inlet filter at `0x005c6bfc` verified that a change requires consecutive matching replies at thresholds 2 and 8. With the currently observed `filter_count=2`, a single absent reply followed by a present reply leaves the inlet present. Two consecutive absent replies followed by two present replies allow the slot to clear and re-arm. The historical capture leaves three questions open: what cleared the slot, which branch sent each request, and how far the filament actually moved.
