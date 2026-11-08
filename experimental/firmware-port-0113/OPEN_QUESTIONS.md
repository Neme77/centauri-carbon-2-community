# Open questions

Ordered by priority. Each entry: what is open, the concrete test that would
close it, and why it matters.

## Point 0 — CRITICAL: the earlier `elegoo_printer.k1` payload body is defective

**What is open:** `WORK/elegoo_printer.k1` (SHA-256
`adb8529358f12c7f5f10dcfdd8a9e30a0c385cb6bfd8b404d8bcb42b5d6abcc6`), the
binary that was already referenced in the GitHub handoff issue, has its
500-byte LAN/WAN coexistence payload built from a template that is **not**
the real 02.01.01.07 payload logic. Comparing it byte-for-byte against
`experimental/firmware-port/network44-manifest.json`'s `after` field for
`va="0xcd77e8"` (the authoritative source of that logic): **353 of 500 bytes
that should hold real instructions are zero** instead. Only the 17 relocated
branch words (68 bytes) and nothing else are populated; the rest of the
coexistence function's body — the `movw`/`movt` address loads, comparisons,
string references, and non-branch instructions that connect those 17
branches into working logic — is missing.

`verify_patched.py`'s 42/42 PASS does **not** catch this, because it only
checks branch *targets*, not payload *content* — a defect to flag separately
rather than assume it invalidates the branch wiring, which remains correct.

**Concrete test that closes it:** `apply_patches.py` (new in this package)
sources the payload from the real manifest and overlays only the 17 branch
words on top of it, producing `WORK/corrected/elegoo_printer.patched` (SHA-256
`2828a99a42993788bbe39675909d832ed67fe1f2ab432d1ae305bad2524f8f4a`). This
passes `stage0_verify.py` (82/82) and `verify_patched.py` (42/42) and is
deterministic (confirmed: two independent runs produce the identical hash).
**Use this file, not `elegoo_printer.k1`.**

**Priority: must-fix before any further step.** This is not a style issue —
running the defective payload on real hardware would execute 17 jumps
connected mostly by inert filler instead of the intended coexistence logic.

**Action needed:** the GitHub handoff issue should be corrected/updated to
reference the new hash instead of (or in addition to) the old one, with a
note explaining the fix. (Not done automatically by this package — see
`HANDOFF_PACKAGE_CHECKLIST.md`.)

## Point 1 — `LanStrategy::start+0x11c` branch target — RESOLVED (2026-11-08)

**What was open:** the payload branch at payload offset `0x58` targets
`0x8267bc` (`LanStrategy::start+0x11c` in the new build). This target was
initially inferred structurally from a 12-instruction window, not a full
disassembly.

**Resolved by:** full Capstone disassembly of `LanStrategy::start` in both
builds (0x81ef10, size 0x284, 02.01.01.07; 0x8266a0, 02.01.01.13) —
**161/161 instructions structurally identical** (mnemonic + normalized
operands; only branch targets and `movw`/`movt` immediates differ, as
expected from relocation). The patch site is at **instruction index 71** in
both functions, both `mov r0,r3` (old `0x81f02c`, new `0x8267bc`), with
matching surrounding control flow (same preceding `bl`s and `sub`s, same
following `bl`/`movw`/`movt` sequence). This is the whole-function
control-flow-graph match the earlier version of this point asked for — not
just the one instruction.

**Priority: closed.** `0x8267bc` is a confirmed branch target, not inferred.

## Point 2 — real HTTP/network/MQTT/camera integration not proven

**What is open:** all four emulation harnesses (E1/E2/E3-E5/E6) intercept
vendor function calls rather than executing them, and none opens a real
socket, drives real hardware, or runs the application's actual event loop.
The emulation proves *control flow* at the specific instructions each
harness touches; it proves nothing about the LAN/WAN coexistence payload's
actual runtime behavior (i.e., whether the restored HTTP token fallback,
MQTT task naming, and network-manager calls inside the payload produce
correct behavior end-to-end), the GUI's visual behavior, or the camera
stream's actual coexistence under load.

**Concrete test that closes it:** the hardware test plan in
`HARDWARE_TEST_PLAN.md`/`HARDWARE_TEST_CHECKLIST.md` — in particular HW-05
(LAN/WLAN behavior), HW-06 (camera + remote), HW-08 (MQTT), HW-09 (upload),
run against the **corrected** binary with a verified rollback path in place
first (`ROLLBACK_PLAN.md`).

**Priority: required before any release decision; this is the maintainer's
step, not something emulation can substitute for.**

## Point 3 — `ported_patch_set` stays `false` pending the above

**What is open:** the builder is fail-closed for base 02.01.01.13 by design
(`builder/current/base_profiles/02.01.01.13.json`). This package does not
request that be changed.

**Concrete test that closes it:** points 0–2 resolved, then a maintainer
decision to flip the flag after a passing hardware test — per `AGENTS.md`
and `docs/TESTING.md`, that decision belongs to the maintainer, not to this
package.

**Priority: gate, not a task — closes automatically once 0–2 are done and a
maintainer signs off.**
