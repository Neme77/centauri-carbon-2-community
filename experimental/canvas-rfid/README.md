# Experimental Canvas RFID reader — draft, not release-ready

This directory preserves source for the physically tested maintainer trial:
host guard, native response capture and guarded Canvas application transfer.
It contains no ELEGOO image, vendor executable, stock GLIBC or signing key.
The production CC2 Control interface stays hidden unless its exact qualified
capture helper is installed. Merging source is not an activation procedure.

## Verified scope

Neme77 tested one physical Anycubic PETG tag on Canvas application .44, native
CC2 executable SHA256 19712b5943b10bb8cb6aab9a4ed498ee962b28538b23c91b7ed003eb9e590745.
The host correction accepts the real 195-byte native response and the legacy
191-byte shape. Capture keeps UID and exactly 180 tag bytes. Native ELEGOO
responses are forwarded unchanged; foreign-tag data is isolated from the native
ELEGOO-only decoder. No tagged ELEGOO spool was available for physical testing.

The owner verified reading and explicit Canvas slot assignment. Material data
and physical inlet presence are independent: a slot can have assigned material
while remaining translucent until its filament is detected at the Canvas inlet.
This does not require loading the filament all the way to the nozzle.
No claim is made that every Anycubic, NFC or RFID format works.

## Panel behavior

The collapsed RFID control sits on the right of Canvas Actions. A single bounded
capture produces a preview; users explicitly select a slot and confirm. Only
material, product, maker, colour and nozzle limits are sent through the existing
protected material action. No heating, motion, loading or inferred weight occurs.
Bed range is informational; remaining filament and weight remain unknown.
The UI distinguishes a submitted request, native metadata readback and physical
inlet presence, instead of pretending a queued request is a confirmed write.

Start/apply require fresh idle/cold state. A preview is single-use and expires
after 120 seconds or a native identity/state change. Native UDS remains fresh
within five seconds during read-only continuation; the existing fifteen-second
MQTT allowance prevents treating a routine sixth-second snapshot age as a print
transition. Preflight cancellation waits for bounded guard cleanup before
interrupting the capture, preventing an orphaned manager lock.

## Autonomous activation and restoration

The closed-profile private PC trial now prepares the host guard, requires a full
kernel reboot and verifies the loaded guard before transferring the Canvas
application. It then requires a second full reboot and live Canvas verification.
The original image is backed up durably before transfer. The persistent lifecycle
and guarded startup handle interrupted operations; a protocol ACK never claims
physical flash readback. Restoration transfers the original Canvas first, verifies
it after a new boot, then restores the original host startup and verifies again.

Neme77 physically completed that activation, scan, original-Canvas restoration
and host-removal cycle on a freshly reinstalled 4.4.3. A subsequent complete
1.2.26 private trial verified autonomous activation, RFID, approved fan layout,
flaps and the native EXHAUST minimum together. A Creality spool was also read;
this is evidence for that spool, not a universal brand/format compatibility claim.

`cumulative/` preserves the exact source of the PC staging correction: after
checking SHA256, restore mode 0700 on the regular, non-symlink ARM publisher,
because Windows/SCP may transfer it without execute permission. The old completed
RFID component is archived reversibly; active or incomplete sessions are refused.
EXHAUST is staged before RFID, and RFID restoration retains that floor baseline.
The first staging observation can precede loading EXHAUST, but subsequent boot
verification requires both the pinned mapping and its current-PID idle channel.

## Public delivery remains blocked

These are closed, fingerprinted 4.4.3 profiles, not universal installation
permission. No stock Canvas image, native executable, signing key or proprietary
library is distributed here. The ordinary public app updater still does not
install this complete native suite or flash Canvas automatically. Public OTA,
base-firmware migration and broader platform/update coverage must be completed
and reviewed before publishing 1.2.26 or promoting the catalog. Keep this PR draft
until that delivery decision is made. The #301 persistence work is not included.

## Build and tests

`python3 cc2-control/tests/test_rfid.py` exercises the actual API/state machine
and a pinned dummy child on Linux. Set `CC2_TEST_ARM=1` and/or
`CC2_TEST_SANITIZERS=1` for the additional toolchains. The owner UID override is
test-only; production still requires root-owned private runtime files.

`preview/test-rfid.mjs` exercises real browser controls and separate metadata/
presence feedback. Existing private qualification also covered native host/ARM
entry hooks, C++ vector/unwind behavior, original GLIBC 2.23, installer rollback
and transfer failure recovery. Those results apply to the pinned maintainer
trial, not an as-yet-unwritten universal installer.

`host/` is the tested frame-corrected R2 ABI source. `capture.c` uses that ABI.
`transfer/` preserves the earlier R2-qualified original-Canvas restoration core.
Their default component pins intentionally differ: do not compile and combine
them as a public installer without the coordinated lifecycle work above.

## Provenance and acknowledgements

- Anycubic field layout adapted from
  [OpenRFID](https://github.com/suchmememanyskill/OpenRFID), GPL-3.0,
  `src/tag/anycubic/processor.py`, blob
  4823abc89b4f0b5e7b82fe485e480cbf98857b7a. OpenRFID credits
  [DnG-Crafts/ACE-RFID](https://github.com/DnG-Crafts/ACE-RFID).
- [OpenCentauri COSMOS](https://github.com/OpenCentauri/cosmos) supplied a
  reference for comparing RFID handling; its MCU firmware is not included here.
- ELEGOO supplies the proprietary Canvas/printer application and artwork.
  This project does not claim ownership of those components.
- Neme77 designed the UI flow and performed physical Canvas, Anycubic PETG,
  inlet-presence, LAN and EXHAUST coexistence tests.

Public fixtures retain the measured layout but replace the physical tag UID.
