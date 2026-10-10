# Full Canvas filament ejection

This optional component adds **Eject slot** to the CC2 touchscreen filament
page and the CC2 Control web Canvas page. First use the normal **Unload**
operation to withdraw filament from the nozzle. Then select the spool slot and
confirm ejection. Its feeder reverses until that slot's inlet sensor clears,
and stops. A cleared sensor does not prove that every
possible length of external tubing is empty; remove the loose spool end after
the operation.

The web page shows progress and the final result, with English, French and
Italian strings. The touchscreen adds an English button below the existing
controls in the scrolling filament panel. Its first tap asks whether the
nozzle is unloaded; a second tap within five seconds confirms the selected
slot. While running, either interface's button requests a stop. Screen slots
1â€“4 map to protocol channels 0â€“3.

## Qualification status

The adapter targets the exact stock **02.01.00.00** Canvas library and its
community Z-offset-patched GUI, identified in [ABI.md](ABI.md). It is opt-in;
the ordinary web package reports ejection unavailable without the component.
Unknown firmware hashes, stale component builds and mismatching hook entries
are rejected. This is experimental source, not a released or printer-validated
firmware image.

Host state-machine and native button callback tests, the CC2 Control host
suite, the headless web interaction test, ARM compilation with GCC 6.5.0,
and integration against privately held 02.01 ELF files have been checked.
The earlier V01.03.02.36 test only established a short reverse move stopped by
a four-second timeout. It did not validate this sensor loop, either new UI,
or operation on 02.01.00.00. Complete the printer checks below before release.

## Motion and failure handling

- Requires fresh Canvas status (at most 0.5 seconds old), connection, idle
  print state, no toolhead filament, no active feed channel and no slot faults.
- An already clear inlet sensor returns `empty` without moving.
- Rechecks all guards and sends one bounded
  reverse move: 1,200 mm maximum, 15 mm/s, acceleration 100 mm/sÂ².
- Stops on sensor clearance, cancellation, lost/stale telemetry, non-idle
  state, a fault, 90 seconds, 1,200 mm measured travel, or three seconds without
  0.5 mm position progress. The vendor auto-prefeed callback is suppressed
  during ejection to prevent it opposing the reverse move.
- Every path after starting, including exceptions, requests motor stop. A stop ACK is followed by up to one second of status checks
  for physical stop. Completion also rechecks sensor clearance. Failure to
  acknowledge stop reports `stop_failed`; it never reports success.

The routine uses the same feeder transport as stock motor control. It does not
issue separate maintenance rocker-direction commands.

Both interfaces send:

```text
CANVAS_MOTOR_CONTROL CHANNEL=0 EJECT=1 SPEED=0 DISTANCE=0 TIMEOUT=1
```

The adapter handles `EJECT=1` itself. If it is absent, the stock handler sees
`SPEED=0` and requests a stop. This deliberately avoids a default forward move
or reliance on the stock negative-distance comparison. Ordinary motor
commands continue through the stock implementation; the standalone signed
distance fix remains documented in [../canvas-reverse](../canvas-reverse/).

## Build and integrate

Build on Linux or WSL with the ARM GNU GCC 6.5.0 hard-float toolchain:

```sh
python3 experimental/canvas-eject/build.py \
  --compiler /path/to/gcc-linaro-6.5.0/bin/arm-linux-gnueabihf-g++ \
  --output /path/outside/repository/canvas-eject-component
```

The build runs the host sensor-loop tests and checks module symbol-version
requirements against GLIBC 2.23 and GLIBCXX 3.4.22. The manifest records all
runtime source hashes and both module hashes. Do not commit the generated
modules or vendor firmware.

Prepare the current CC2 Control and reactor components using the normal
builder workflow. Add these optional arguments to the normal, fully qualified
`builder/current/core/firmware_builder.py` invocation:

```sh
--canvas-eject-component /path/outside/repository/canvas-eject-component \
--patchelf /path/to/patchelf
```

The builder keeps its existing stock-input, signing and reactor checks. It
adds the runtime as an ELF dependency of `libelegoo_extras.so`, verifying that
all executable section bytes/addresses and resolved dynamic symbol ABI stay
identical. It wraps the GUI launch with a hash-checked screen-module preload,
then installs the existing reactor startup coordination. Installation and
re-extracted-image audits check both modules, the GUI, the modified library
and screen launch wiring. No vendor code bytes are changed by this component.

Creating `/opt/usr/cc2-canvas-eject-disabled` before reboot disables both
adapters. The GUI launcher also falls back to the stock GUI if its qualification
or preload checks fail. A runtime hook fingerprint mismatch stops startup with
exit 126 instead of enabling an unqualified motion routine.

## Required printer validation

Record firmware hashes, module manifest, slot, material, measured movement,
sensor transitions and final result for each observed test:

1. Boot on qualified 02.01 firmware; check normal GUI, web, printing, ordinary
   load/unload, auto-prefeed, and reactor operation before ejection testing.
2. Check all four screen selections and web selections address the same
   physical slot. Verify confirmation cancellation causes no movement.
3. Check an empty slot returns `empty` with no motion. Check nozzle-loaded,
   active-channel, paused and printing conditions reject ejection.
4. After normal unload, confirm each inlet sensor clears during reverse and
   the motor stops. Observe both UIs' progress and result.
5. Cancel from each UI; observe physical stop. Check
   telemetry loss, stall, distance limit and failed command handling under
   controlled conditions. Do not intentionally disconnect a powered motor.
6. Reboot and confirm no cancellation file or stale running state is reused,
   and ordinary loading/auto-prefeed works again. Check the disable-file path.

Host tests cannot establish motor direction, sensor identity, mechanical
clearance or touchscreen layout on the physical printer.
