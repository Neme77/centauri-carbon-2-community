# Optional native exhaust minimum — issue #285

Source-only private opt-in trial. Neme77 completed physical acceptance on
2026-11-04: minimum EXHAUST during a print, Automatic release, invalidation on
cancellation, and loading after full reboot in both Wi-Fi and LAN configurations.
These observations are separate from the simulated host/ARM qualification.

The normal application/firmware packages do not automatically install or enable
this optional native payload. Public updater integration remains separate work;
disable the private trial before applying another application or firmware update.

Automatic control remains the default. An explicit whole-percent request sets a
minimum for the current native print, for example `max(native cooling speed,
60%)`. The firmware may increase cooling. Zero releases the request and returns
to Automatic, without forcing the fan below its native minimum.

The native process owns a random nonce and a print-state generation. Pause,
completion, cancellation, unknown states and a process restart invalidate the
request. The native timer, chamber-hold branch, thermal callbacks and post-print
cooling remain intact. No recurring fan G-code is sent. The existing EXHAUST
slider continues to show the observed speed; the new controls show the requested
minimum, which is considered on the next normal native cycle (up to 10 seconds).

The optional library is restricted to the exact qualified community executable
and two verified ARM instruction prefixes. It preserves the native C++ argument
and calls the original print-state handler through a relocated prefix. The
cooling-choice trampoline retains caller registers and its native stack frame.
It is inactive in foreign executables or when qualification/secure IPC fails.
Only the supported native cooling branch receives the minimum.

`prepare_payload.py` builds an opt-in private activation component. `manage.sh`
checks fresh cold Idle, native print state and update/network/calibration
transactions. It retains exact file contents and permissions for the runtime
launcher and, when installed, the Connections profile and both checksum lists.
Activation does not restart services: a complete manual reboot is required.
Missing or changed addon files fall back to the existing native reactor launcher
without loading the optional library. `disable` restores only the exact retained
originals and refuses foreign edits. Kernel, drivers, executable, original
reactor library, display and calibration files are not replaced.

This private activation must be disabled before an ordinary application or
firmware update. Public updater integration remains required before release.

Qualification covers:

- `test_policy.c`: automatic/minimum policy and native higher-speed preservation;
- `test_native.c`: actual C forwarding entry with dummy state events, secure IPC,
  current job ownership, stale/foreign requests and pause/end invalidation;
- `test_arm_abi.c`: actual ARM trampoline, relocated prefix and hard-float return
  against dummy mapped instructions; no vendor code is run;
- `test_string_abi.cpp` plus its C bridge: compiler-generated C++ by-value string
  ABI, native forwarding, lifetime and exception unwinding;
- `test_cavity_floor_api.c`: current native identity, fresh printing telemetry,
  bounded request data, rejection and command-free Automatic release;
- `test_manage.py`: actual private manager on dash/BusyBox with fake HTTP/files,
  native/LAN manifests, cold/Idle guards and exact restoration;
- `preview/test-cavity-fan-floor.mjs`: actual built browser controls and responsive
  layout. Source and executable fixtures do not substitute for hardware testing.


Boot wiring correction: qualify the optional preload before exporting the
reactor preload, and wire both the runtime launcher and the network startup
copy. Preserve the existing network shim order and flags. The private manager
also owns the active network activation script and restores all six original
targets exactly. Already-mapped network transitions may retain the optional
preload; native service respawning through the legacy network helper is refused
while this private trial is enabled. Full public native-spawn integration is
still pending. `test_boot_loader.py` executes real loader scripts with a dummy
ELF and a preload constructor that rejects foreign helpers; no vendor executable
is run. Passing mocks or source checks alone did not detect the earlier wiring
errors and is not sufficient evidence for this startup path.

## Reviewed boot migration

`upgrade_boot.sh` migrates only the exact accepted R1b four-target private
installation to the six-target boot wiring. It refuses foreign/mapped payloads,
retains the previous component and exact rollback files, and never restarts a
process. Replace its payload manifest placeholder only with the locally qualified
manifest. `test_upgrade_boot.py` requires the retained owner-trial inputs under
`/input`; those fixture archives are not shipped in this source repository. The
24 fake migration cases, 32 real-loader dummy executions and 50 manager cases
passed before the owner performed the successful migration.
