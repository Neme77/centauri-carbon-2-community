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

## Release blocker: autonomous activation/update lifecycle

There is deliberately no public activation installer yet. The private trial
used nested legacy R2/EXHAUST/LAN profiles and a separately verified stock image.
Those owner-specific migration assumptions must not be treated as a general
public installation profile. Before a 1.2.26 release, complete and test:

1. One unattended-after-explicit-opt-in transaction, with fresh idle/cold and
   exact platform checks; unsupported hardware refuses before any stop/write.
2. Root-private verified original Canvas backup and recoverable host startup
   transaction, preserving reactor, camera, LAN, EXHAUST, display and settings.
3. Qualified host guard installed and boot-verified before any foreign-tag
   firmware change, plus recovery after interrupted staging or failed transfer.
4. Application-only Canvas transfer; preserve bootloader and power-recovery
   policy. This protocol has acknowledgements, not physical flash readback.
5. Original-Canvas restoration before removing its host guard, and a coordinated
   ordinary application/base-firmware update path; preserve rollback dependencies.
6. No manual SSH commands, repeated archive repairs or owner-only backup names
   in the public flow. A necessary full reboot must remain clearly visible.

No default app update in this draft flashes the Canvas. Do not publish a release
or promote a catalog until this lifecycle has been physically qualified.

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
