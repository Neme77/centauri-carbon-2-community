# Runtime findings, 2026-11-08 (hardware observation, no write actions)

Reported by the user from a live device, after installing the real-hardware
crash fix (`688b7bf8…`, see `OPEN_QUESTIONS.md`'s 2026-11-08 entry and
`CRASH_FIX_MESSAGE.md`). This document only records what was observed and
confirms it against the install-time/runtime source in this repo. **No hash
value was changed, no file was written to the device, and no 02.01.01.13
update was applied as part of this.**

## 1. Fix build confirmed running

- PID 7793 `elegoo_printer`, `/proc/7793/exe` → `/opt/bin/elegoo_printer`,
  SHA-256 of both the running exe and the on-disk binary:
  `688b7bf8a27762d56b35d9738ea5b0c3d45580b402e122b0f77ee49dab1a8cfd` — matches
  `WORK/corrected_v2/elegoo_printer.patched` in `README_PORT.md`'s "current,
  USE THESE" row. The superseded, hardware-crashing build
  (`2828a99a42993788bbe39675909d832ed67fe1f2ab432d1ae305bad2524f8f4a`) exists
  only as an old USB copy, not installed.

## 2. Reactor runtime is in fallback (expected, not a new defect)

`/etc/init.d/cc2-reactor-launch` (installed from
`builder/current/components/reactor/firmware-launcher.sh`) gates activation on
`sha256sum /opt/bin/elegoo_printer` equalling the one pinned
`VENDOR_SHA256` in `builder/current/core/reactor_component.py`:
`19712b5943b10bb8cb6aab9a4ed498ee962b28538b23c91b7ed003eb9e590745` — the
finished Community V4.4.3 binary built from base 02.01.01.07. `688b7bf8…` is
a different build (base 02.01.01.13, different bytes, different hash), so the
gate correctly takes the `fallback()` path: `exec /opt/bin/elegoo_printer
"$@"` with no `LD_PRELOAD`. Observed on the device: no `LD_PRELOAD` in PID
7793's environment, and `libcc2-reactor-v2.so` absent from
`/proc/7793/maps` (only `/opt/lib/libcolib.so` present) — consistent with the
gate working as designed.

This is **expected fail-closed behavior for a binary the reactor was never
qualified against**, not a bug introduced by the crash fix. The reactor was
never built or address-mapped for 02.01.01.13 in the first place (see
`REACTOR_CAMERA_PORT_TASKS.md`).

## 3. Camera runtime is in fallback (same pattern)

`/opt/bin/ai_camera` SHA-256 `90da7634c21b74b3bf943d68129da78668b47f9706620e
158283697ab95553a7` (the one-word pause→resume coexistence fix for
02.01.01.13, see `PATCH_SITES.md`) does not match `CAMERA_SHA` in
`builder/current/core/camera_component.py`
(`ec5633b17de329d5b18c3f8d8de6ebf389391a04193536d976ba167022d07b99`, the
Community V4.4.3 camera build). `/tmp/cc2-camera-firmware-status.txt` on the
device reports `mode=native reason=camera-fingerprint`, confirming the same
gate took the same path, and `grep libcc2|libcolib /proc/7762/maps` shows
nothing — no camera hook library loaded. `video_present=true` / USB
`a108:2240` in the heartbeat only shows the camera device node is present; it
is not evidence the community camera hooks (LAN MJPEG resume, recovery
broadcast) are active, because they are not.

## 4. Preflight log caveat, confirmed as written

`/opt/usr/cc2-reactor-runtime-v2/preflight.log`'s own text — "vendor entry
calls mocked; original ARM function binding is separately
fingerprint-audited and requires runtime testing" — matches
`builder/current/tests/test_reactor_firmware.py`'s actual scope: that suite
exercises the Python installer/hash-gate logic and the shell boot-hook's
fallback decision tree via real subprocess execution against a *fixture*
binary standing in for the vendor. It does not execute `reactor_module.cpp`
on real ARM hardware or in an emulator, and does not validate the four
hardcoded patch addresses against a real vendor binary. A passing preflight
log therefore says nothing about whether reactor hooks are active in the
current process, nor about compatibility with `688b7bf8…` — which is exactly
what was observed (preflight log present, hooks not loaded).

## 5. No write action taken, no hash value changed

- `builder/current/base_profiles/02.01.01.13.json`'s `ported_patch_set`
  remains `false`, as already documented in `README_PORT.md`'s Status
  section — unchanged by this observation.
- `VENDOR_SHA256`, `LIBCO_SHA256`, `CAMERA_SHA`, `INIT_SHA`, and `PINS` in
  `builder/current/core/reactor_component.py`,
  `builder/current/core/camera_component.py`, and
  `cc2-control/installer/camera_component.py` are all unchanged.
- No OTA/update to 02.01.01.13 (or any other base) was applied to the
  device as part of this finding.

## 6. Open items carried forward separately

- Memory (OOM kill of the GUI, root cause not identified) is tracked as its
  own, unrelated issue — not addressed here.
- `02.01.01.13` package provenance (`CC2_V4_4_3_STOCK_Control_1_2_25.zip.sig`
  vs. the official OTA package) remains unconfirmed identical; no action
  taken on that question here.
- What it would take to re-enable the reactor and camera hooks against the
  new, 02.01.01.13-based binaries is scoped as two separate porting tasks in
  `REACTOR_CAMERA_PORT_TASKS.md`.
