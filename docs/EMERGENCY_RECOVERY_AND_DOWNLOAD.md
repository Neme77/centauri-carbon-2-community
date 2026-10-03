# Emergency recovery and G-code downloads

CC2 Control offers **Restart printer** beside Emergency Stop in the top bar.
The button is enabled only after this CC2 Control process has successfully sent
an exact `M112` script to the local printer socket. Sending a request that fails
before that point does not enable recovery. This is a shared backend latch:
all browser instances see it through `/api/printer`, and clearing the console
does not reset it. A CC2 Control process restart resets the latch.

Confirming recovery sends `POST /api/recovery/reboot` with the exact body
`REBOOT_AFTER_EMERGENCY` and the normal browser mutation marker. The server
checks the latch again, rejects duplicate requests, and waits two seconds before
launching a fixed local `reboot` utility. It does not invoke SSH or accept shell
commands. This reboots the whole printer, including its display. Wait for the
printer and CC2 Control to reconnect. `FIRMWARE_RESTART` alone did not restore
the display in the hardware investigation and is not used here.

The latch means that M112 was transmitted, not that the firmware acknowledged a
shutdown. It is intended for recovery immediately after a CC2 Emergency Stop;
it does not detect an emergency stop issued by another application. If the
reboot utility fails, `recovery.error` reports the failure and a retry is allowed.
Printer validation of the new full-system recovery button is still required.

**Download G-code** is available in each Files row and the selected-file panel.
The browser handles the native attachment without buffering the entire file in
JavaScript. The read-only endpoint is:

`GET /api/gcode-files/download?storage=internal|usb&file=<encoded-relative-path>`

Downloads work independently of print state. Only regular `.gcode` files below
the configured internal or USB root are accepted. Each path segment is opened
relative to its parent with `O_NOFOLLOW`; traversal, symbolic links, special
files, malformed escapes and duplicate query fields are rejected. The filename
uses UTF-8 attachment encoding. Existing HTTP Host/Origin checks still apply.

The service streams 16 KiB chunks on at most two temporary workers with 64 KiB
stacks. A stalled write expires after ten seconds and a transfer has a five-minute
budget. HTTP downloads therefore do not run in the MQTT/UDS event loop.

Validation:

- Host suite: `make clean test CROSS= CC=gcc`.
- UI checks/build: `npm run check && npm run build` in `cc2-control/web-src`.
- Browser layout: `npm run test:preview`.
- New interactive paths: `npm run test:recovery` (simulated restart only).
- Printer: verify disabled recovery before Emergency Stop, confirm/cancel after
  Stop, full display recovery after reboot, and downloads from internal/USB
  storage during an active print. Do not merge on host tests alone.
