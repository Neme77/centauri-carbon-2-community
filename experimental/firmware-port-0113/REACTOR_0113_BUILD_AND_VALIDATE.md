# Reactor 02.01.01.13 — exact .so rebuild and active hardware validation

Operator guide for the two steps the builder cannot do: rebuilding
`libcc2-reactor-v2.so` from the committed 0113 sources with the qualified
toolchain, and validating the **active** reactor on the printer. The device
reactor is a Community add-on and is gated off by default; these steps turn it
on deliberately and reversibly. Do them on an **idle** printer with the rollback
ready. The `.so` is never committed.

Context: fork 2's 0113 `.so` (`8f78646d…`) could not activate because its
`fingerprints.h` kept the 07 addresses (device-verified; see
`reactor/README.md`). The committed sources correct that, so the rebuilt `.so`
is expected to pass the self-check — which is exactly what step 2 confirms.

## 1. Rebuild the .so (host with the ARM toolchain)

Prerequisites (same as the 07 reactor build, `docs/BUILD.md`):
- the qualified cross toolchain at
  `/opt/cc2-cross/toolchain-out/bin/arm-cortex_a15-linux-gnueabihf-g++`
  (GCC 6.5.0, glibc 2.23);
- the vendor link stub `experimental/firmware-port/reactor/vendor-link/libcolib.so`
  (SHA-256 `14288a4a…`), supplied locally, never committed;
- a clean, committed tree (the sources are hash-pinned).

Build:

```sh
python3 builder/current/prepare_reactor.py --base-version 02.01.01.13
```

This compiles from the 0113 sources (`reactor_module.cpp` `21ef738b…` +
`fingerprints.h`, with `bridge_test.cpp`/`co_routine.h` shared with 07, staged
so the bridge includes the 0113 module), runs the ARM/unwind/glibc checks, and
writes `builder/current/components/reactor/prepared/` plus
`prepared-manifest.json`. Confirm:

```sh
python3 - <<'PY'
import json
m=json.load(open('builder/current/components/reactor/prepared-manifest.json'))
assert m['vendor_sha256']=='688b7bf8a27762d56b35d9738ea5b0c3d45580b402e122b0f77ee49dab1a8cfd', m['vendor_sha256']
assert m['sources']['reactor_module.cpp']=='21ef738ba6a71312c4f1766414a49d0fb7c0627d0fe79b2766120620754620fa'
print('prepared-manifest OK; .so', m['files']['libcc2-reactor-v2.so'][:16])
PY
```

The builder then validates this component via `reactor_component` with
`base_version='02.01.01.13'` and `vendor_sha=688b7bf8…` (already wired).

## 2. Validate on the 0113 printer

Install the runtime under `/opt/usr/cc2-reactor-runtime-v2/` and the boot hook
using the existing rollback-safe procedure (upload under a new name, verify
hashes, `mv` into place, restart — see `reactor/ARM_TEST_PLAN.md` §3 and
`TEST_MATRIX.md`). The runtime directory must contain the rebuilt files and a
copy of the patched 0113 printer (`elegoo_printer`, `688b7bf8…`) so the launcher
and preflight can verify it.

### 2a. Safe preflight (no change to the running printer)

`preflight.sh` loads the `.so` with `CC2_REACTOR_RUNTIME_V2=0`, so the
constructor does **not** patch anything; it only proves the `.so` loads, the
bridge entry points resolve, and `libcolib`/`SHA256SUMS` match:

```sh
ssh root@192.168.2.220 'cd /opt/usr/cc2-reactor-runtime-v2 && sh preflight.sh'
# expect: "PREFLIGHT ARM PASS: nessun servizio modificato."
```

A failure here (libco mismatch, unresolved symbol) stops the port before any
active test.

### 2b. Active test (patches the live printer — the real check)

The launcher sets `CC2_REACTOR_RUNTIME_V2=1`, preloads the `.so`, and execs the
printer; the constructor then checks the four fingerprints **at the corrected
0113 addresses** (`0x26d08c/0x26f1dc/0x2708d8/0x26cb70`) and, on success,
patches them and writes `activation.marker`.

On an **idle** printer, enable the reactor (remove the disable marker so the
boot hook runs the launcher) and restart, per `ARM_TEST_PLAN.md`:

```sh
# enable + restart (idle printer only)
ssh root@192.168.2.220 'rm -f /opt/usr/cc2-reactor-disabled; /etc/init.d/printer restart'
# success criteria
ssh root@192.168.2.220 'cat /opt/usr/cc2-reactor-runtime-v2/activation.marker 2>/dev/null; echo "---"; pidof elegoo_printer; grep -c reactor /proc/$(pidof elegoo_printer)/maps 2>/dev/null'
```

Pass when **all** hold:
- `activation.marker` exists and records `pid=`, `callback=`, `timers=`,
  `poll=`, `wait=` — this is the proof the fingerprint self-check passed (with
  fork 2's `fingerprints.h` it never would), i.e. the device-verified fix works;
- `elegoo_printer` is running (a fresh PID, stable for minutes, no crash loop);
- the `.so` is mapped into the process (`/proc/<pid>/maps` shows
  `libcc2-reactor-v2.so`);
- the printer operates normally through one idle → print → idle cycle (UI, MQTT,
  camera, a short real print).

Fail-closed is safe, not a brick: if the printer hash, `SHA256SUMS`, or a
fingerprint does not match, the launcher/constructor exits 125 (`fail(...)`) and
the **vendor** printer runs unmodified. Treat that as a stop, diagnose, do not
force.

### 2c. Rollback (always available)

```sh
ssh root@192.168.2.220 'touch /opt/usr/cc2-reactor-disabled; /etc/init.d/printer restart; pidof elegoo_printer'
```

The disable marker makes the boot hook start the plain vendor printer; a clean
boot with the marker present was already verified (H7). Keep the marker in place
until an active run has passed 2b and a full print has been observed.

## Done = release-ready for reactor

Only after 2a + 2b pass on hardware, and the maintainer accepts the result, does
reactor count as ported for 0113. That is separate from flipping
`ported_patch_set` / `QUALIFIED_BASES`, which also requires the other
components and a hardware test of a **builder-produced** image.
