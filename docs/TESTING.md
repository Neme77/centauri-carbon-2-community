# Testing

Host tests and real-printer validation are recorded separately. A host test does not prove hardware safety, and a successful manual print does not replace a regression test.

## CC2 Control host tests

```sh
cd cc2-control
make clean test CROSS= CC=gcc
```

The suite covers file operations, uploads, Panda compatibility, preferences, translations, thermal layout, light state, launch scripts and static UI regressions. Native C harnesses are compiled by their Python drivers where required.

The same host tests, an ARM cross-build and the builder tests run automatically on every pull request, every commit to the `main` and `develop` branches. (`.github/workflows/ci.yml`).

The same host tests, an ARM cross-build and the builder tests run automatically
on every pull request and push to `main` (`.github/workflows/ci.yml`).

CI also treats compiler warnings as errors, runs the suite under sanitizers and starts the ARM binary under qemu. To reproduce locally:

```sh
make clean test CROSS= CC=gcc EXTRA_CFLAGS=-Werror
make clean test CROSS= CC=gcc SANITIZE=address,undefined
make clean test CROSS= CC=gcc SANITIZE=thread
make clean all && CC2_TEST_RUNNER=qemu-arm python3 tests/test_smoke.py dist/cc2-control/cc2-control
```

The runtime tests do not check the exit status of the server they start, so set `ASAN_OPTIONS`, `UBSAN_OPTIONS` or `TSAN_OPTIONS` to `log_path=/tmp/sanitizer/report` and look for report files after the run, as CI does.

## Builder tests

Tests that do not require restricted inputs can run directly:

```sh
python3 -m unittest discover -s builder/current/tests -p 'test_*.py'
python3 builder/current/launch_helpers/tests/test_launcher.py
```

Tests involving stock packages, vendor reference binaries or signing keys must receive those inputs locally. Missing restricted inputs should cause a skip or a clear fail-closed error, never a silent fallback.

## Real-printer release checklist

- clean boot and supervised CC2 Control startup;
- first-run LAN-code registration and local service restart;
- MQTT and Canvas reconnect without restarting printer services;
- OrcaSlicer upload, spool selection, print start and cancel;
- filenames containing spaces and multiple-file operations;
- live temperatures, manual nozzle/bed heating and fans;
- thumbnails, elapsed/remaining time, layer totals and object statistics;
- Side A/Side B saved meshes, adaptive mesh and screw levelling;
- camera aspect ratio, English/Italian text and both themes;
- protected console, emergency stop and unsafe-state guards;
- power-cycle persistence and update/rollback behaviour.

Record the tested firmware hash, source commit, printer base version and any required post-install steps with the release.
