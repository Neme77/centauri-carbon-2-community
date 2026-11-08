# Reactor and camera hook ports to 02.01.01.13 — two separate tasks

Scoped in response to the runtime findings in `RUNTIME_FINDINGS_20261108.md`:
both the reactor runtime and the camera hooks are currently in native
fallback against the 02.01.01.13-based `elegoo_printer` (`688b7bf8…`) and
`ai_camera` (`90da7634…`) builds, because neither was ever built or
address-mapped for this base. Each is independent work from the HTTP/LAN-WAN
payload port already done in this directory. **This document is analysis
and task scoping only — no code, address table, or binary is produced here,
no hash constant is changed, and nothing is written to a device.**

Mechanism and address data below come from reading
`builder/current/core/reactor_component.py`,
`builder/current/components/reactor/firmware-launcher.sh`,
`experimental/firmware-port/reactor/reactor_module.cpp`,
`experimental/firmware-port/reactor/port-manifest.json`,
`builder/current/prepare_reactor.py`,
`builder/current/core/camera_component.py`,
`builder/current/components/camera/firmware-launcher.sh`,
`cc2-control/installer/camera/boot-camera.sh`,
`cc2-control/installer/camera_component.py`,
`experimental/camera-client-reap-r1/camera_reap.c`,
`experimental/camera-recovery-r2/camera_recovery.c`, and their `build-ci.py`
scripts.

## Mechanism (confirmed, not pure LD_PRELOAD-symbol-override)

Both the reactor and camera hooks use the same pattern: `LD_PRELOAD` gets the
`.so` mapped into the vendor process, but the actual hooking is an
`__attribute__((constructor))` that runs before `main()` and:

1. Confirms it is running inside the one pinned executable path and that the
   binary's own SHA-256 matches the one qualified build.
2. Checks fixed, hardcoded virtual addresses for an exact "instruction
   fingerprint" (the original opcode bytes) before touching anything —
   refuses (fallback / `_exit(126)`) on any mismatch.
3. `mprotect`s the target `.text` page `RW+EXEC`, overwrites a 4- or 8-byte
   call-site/instruction with a branch or absolute-jump trampoline into its
   own C function, `__builtin___clear_cache`s, restores `R+EXEC`.

This is in-process self-patching of the vendor binary's own code at fixed
addresses — not GOT/PLT rewriting, and not a dlopen-and-call wrapper. It
means every hardcoded address in the source is tied to one exact vendor
binary's layout; a different build (different base, different compiler
output) invalidates all of them even if the call is logically identical.

## Task A — Reactor port to 02.01.01.13

**Scope:** 4 internal vendor methods replaced wholesale:
`ReactorCallback::invoke`, `SelectReactor::check_timers`,
`PollReactor::dispatch_loop`, `ReactorCompletion::wait` (patch targets
`0x26ab5c`, `0x26ccac`, `0x26e3a8`, `0x26a640` in the currently-qualified
02.01.01.07 base) plus roughly 16 further hardcoded addresses the
replacements call back into (vendor `shared_ptr` ctor/dtor, a `now()` time
function, `poll()`, red-black-tree layout offsets, etc. — see
`reactor_module.cpp`'s `original<F>()` call sites).

**Required artifacts, none of which exist yet for 02.01.01.13:**

1. **A new `port-manifest.json`** with a `mapping` of every one of those
   ~20 addresses, old (02.01.01.07) → new (02.01.01.13) VA, in the same
   shape as the existing file, plus the same
   `replaced_functions_comparison` table (`old_va`, `new_va`, `old_size`,
   `new_size`, `instructions_equal_except_call_targets`) for the 4 replaced
   methods against the new binary. The existing manifest should first be
   checked for its own internal inconsistency — it records `vendor_sha256`
   `8039081f9c4b01e720d838d4ab6e4515a8979b9a71a33bd4164321c282e06074`, which
   does not match `VENDOR_SHA256` pinned in `reactor_component.py`
   (`19712b…`) — before using it as a template.
2. **8-byte instruction-fingerprint bytes** at each of the 4 new patch
   addresses, read from the real 02.01.01.13 `elegoo_printer`, to replace
   the `fingerprints` table in `reactor_module.cpp`.
3. **ABI re-verification**, not just address translation: the RB-tree
   layout offsets (`map header +4, root +8, node key +16, shared value
   +20`) and the `shared_ptr`-ABI helper (`struct Shared`) are both
   compiler/struct-layout-dependent, not just address-dependent — these
   need confirming against the new build's actual object layout, not
   assumed stable because the ABI note elsewhere says the compiler/glibc
   requirement (GCC 6.5.0 / glibc 2.23) is unchanged.
4. **A rebuilt `libcc2-reactor-v2.so`**, via `prepare_reactor.py`'s existing
   driver, which requires the `arm-cortex_a15-linux-gnueabihf` GCC 6.5.0 /
   glibc 2.23 cross toolchain at `/opt/cc2-cross/toolchain-out/bin/...`.
   This toolchain is **not present on this machine** (confirmed: `which
   arm-linux-gnueabihf-gcc`/`-g++` and `ls /usr/local/bin | grep -i arm` all
   came back empty — note `prepare_reactor.py` doesn't even look for the
   generic `arm-linux-gnueabihf-*` name, it hardcodes the Cortex-A15-specific
   prefix at that absolute path). Producing this artifact needs that
   toolchain obtained or built first.
5. **A new qualified `VENDOR_SHA256`/`LIBCO_SHA256` pin** for
   `reactor_component.py` and `firmware-launcher.sh`, pointing at the real
   02.01.01.13 `elegoo_printer`/`libcolib.so` hashes — only after 1–4 pass.
6. **Real ARM execution testing**, not just the existing Python/shell test
   suite. `test_reactor_firmware.py` today only exercises the
   installer/hash-gate logic and the shell fallback decision tree against a
   fixture stand-in binary (confirmed by reading it); it never runs
   `reactor_module.cpp` on real hardware or under emulation. The existing
   `port-manifest.json` for the *current* 02.01.01.07 base is itself still
   marked `"status": "experimental_unqualified_until_ARM_tests"` — i.e. even
   the base case has not cleared this bar yet, so a 02.01.01.13 version
   starts from the same unmet requirement, not from a qualified baseline.

**Not in scope / explicitly not done here:** no address was looked up in the
new binary, no `.so` was built, no hash constant was changed.

## Task B — Camera hooks port to 02.01.01.13

**Scope:** two separate libraries, `libcc2-camera-reap-r2.so` (2 patched
call sites: `SPAWN_SITE` client-thread-spawn reap fix, `ENABLE_SITE` LAN
MJPEG/RTC re-enable flag) and `libcc2-camera-recovery-r2.so` (8 patched call
sites for synthetic "camera inserted" broadcast on fresh boot).

**Required artifacts, none of which exist yet for 02.01.01.13:**

1. **No manifest-based address mapping exists even for the current base** —
   unlike the reactor, `camera_reap.c`/`camera_recovery.c` hardcode every
   address as C macros/array literals (`SPAWN_SITE=0x163cd4`,
   `ENABLE_SITE=0x162f28`, `HANDLE_ADDRESS`, `REMOVE_ADDRESS`, the 8-entry
   `sites[]`/`expected[]` opcode-checked array, plus 5 more call-back
   addresses: `native_assign`, `native_accept`, `native_start`,
   `native_broadcast`, `native_send`). A 02.01.01.13 port needs every one of
   these re-derived by disassembling the new `ai_camera` and finding the
   equivalent call sites/opcodes — there is no automated tool for this step
   today (unlike the reactor's `port-manifest.json` approach).
2. **New expected-opcode arrays** for both files' fingerprint/guard checks,
   read from the real new binary at each new address.
3. **Two rebuilt `.so` files plus the native self-test binary**
   (`cc2-camera-reap-check`), via the existing `zig cc -target
   arm-linux-gnueabihf.2.23 -mcpu=cortex_a7` cross-build (confirmed present
   in `experimental/camera-client-reap-r1/build-ci.py` and
   `experimental/camera-recovery-r2/build-ci.py`) plus `qemu-arm` validation
   of the ELF class/machine/EABI flags and GLIBC symbol-version ceiling —
   this toolchain (`zig cc` + `qemu-arm`) is a separate dependency from the
   reactor's GCC cross toolchain and would need confirming/installing
   separately.
4. **New `CAMERA_SHA`** for `camera_component.py`/`common.sh` (the real
   02.01.01.13 `ai_camera` hash — note the *current* `CAMERA_SHA` is itself
   the output of a one-byte patch documented in
   `experimental/firmware-port/camera44-manifest.json`, applied upstream
   before this hash is ever checked — not applied by `camera_component.py`
   itself; equivalent provenance needs establishing for 02.01.01.13's
   `90da7634…`, i.e. confirming it is the agreed "qualified" input the new
   `CAMERA_SHA` should pin, not a separate step).
5. **New `PINS` entries** (filename→sha256) in
   `cc2-control/installer/camera_component.py` for the three rebuilt
   runtime files.
6. **`INIT_SHA` re-check**: the pinned stock `/etc/init.d/printer` hash
   (`bcabb1fa…`) gates the `ai_camera &` → `cc2-camera-launch` substitution;
   confirm whether the 02.01.01.13 base's stock init script is byte-identical
   before assuming this pin carries over unchanged.
7. **Real ARM/hardware or emulated testing** of both libraries against the
   new binary — the existing `build-ci.py` scripts already validate under
   `qemu-arm`, including the "refuses outside the pinned binary" check, but
   that validation needs re-running against the new fingerprints/addresses
   once produced, not assumed to transfer from the 02.01.01.07 qualification.

**Not in scope / explicitly not done here:** no address was looked up in the
new binary, no `.so` was built, no hash constant was changed.

## Common prerequisite for both tasks

Before any address lookup begins, resolve the one documented
inconsistency that affects trust in the existing reactor manifest as a
template: `port-manifest.json`'s `vendor_sha256` does not match
`reactor_component.py`'s `VENDOR_SHA256`. Carrying that discrepancy forward
unexamined into a new 02.01.01.13 manifest would repeat it rather than fix
it.

## Status

Both tasks are scoped, not started. `builder/current/base_profiles/
02.01.01.13.json`'s `ported_patch_set` stays `false`. No hash constant in
either component's source was changed by this document.
