# Open questions

Ordered by priority. Each entry: what is open, the concrete test that would
close it, and why it matters.

## 2026-11-08 real-hardware crash — RESOLVED

**What happened:** The first corrected build (`WORK/corrected/elegoo_printer.
patched`, SHA-256 `2828a99a42993788bbe39675909d832ed67fe1f2ab432d1ae305bad2
524f8f4a`) was flashed and run on a real Centauri Carbon 2. `elegoo_printer`
crashed reproducibly with `SIGSEGV`, `pc = 0x00000000` (jump to NULL), in the
`DeviceStatus`-worker thread, shortly after "RTC WAN streaming enabled" /
"client1 disconnected". Reported registers: `lr = 0x00cc50c4`, `r4 = 0`,
`r10 = 1`.

**Root cause, confirmed:** the port's "17 branches" list (`GROUP_B`), used
everywhere to build the 500-byte LAN/WAN coexistence payload, was itself
incomplete. The payload actually contains **6 more branches and 2 absolute
string-pointer loads** whose encoding was never recomputed for the new
payload base — they still pointed at the OLD (02.01.01.07) build's
addresses:

| Payload offset | Was (stale target) | Should be | Confirmed by |
|---|---|---|---|
| `0x00c`, `0x018`, `0x024`, `0x030` | `0x7ffc5c` (unrelated template code) | `0x819c28` (`NetworkManager::set_mode+0x68c`) | `nm -C` on old/new stock + structural match |
| `0x050` | `0x80c950` (unrelated) | `0x82691c` (`LanStrategy::start+0x27c`) | same |
| `0x11c` | `0x87b538` (unrelated logging code) | `0x895504` (`post_change_message+0x304`) | same, **this is the crash site** |
| `0x1d0`/`0x1d4`, `0x1e8`/`0x1ec` (movw/movt) | `0xcd7974`/`0xcd798c` (land in `spdlog::details::days` in the new build) | `0xcc5138`/`0xcc5150` (the payload's own relocated strings) | byte comparison |

The `0x11c` branch directly and plausibly explains the crash: it is the
instruction right after a known-good `bl get_mode()` at offset `0x114`,
whose return address (`0xcc50c4`) matches the crash's reported `lr` exactly.
Taken only when `get_mode()` returns non-zero (LAN/WAN mode active — matches
the "RTC WAN streaming enabled" correlation), it jumped into unrelated code
containing only further `b` instructions, so `lr` stayed frozen at the
crash's value while execution ran through code with mismatched register/
stack conventions — consistent with eventually faulting on an indirect jump
through a garbage register, `pc = 0`.

**Why this slipped through:** `verify_patched.py` and every porting doc only
ever enumerated and checked the *known 17* branches' targets; nothing
scanned the rest of the payload for branches or absolute-address loads at
all, so these 6 extra branches and 2 stale string pointers were invisible to
every static check and emulation performed before the real-hardware test —
including the 42/42 "PASS" already referenced in GitHub PR #336 and issue
#334.

**Fix:** `scan_payload_branches.py` (new, read-only) systematically
disassembles the full payload and cross-checks every branch/absolute-address
load against `nm -C` on both stock builds, finding exactly the 8 sites
above (output: `SCAN_PAYLOAD_BRANCHES_OUTPUT.txt`). `apply_patches.py` now
applies `EXTRA_BRANCHES` and `STRING_RELOCS` on top of `GROUP_B`, producing
`WORK/corrected_v2/elegoo_printer.patched` (SHA-256
`688b7bf8a27762d56b35d9738ea5b0c3d45580b402e122b0f77ee49dab1a8cfd`).

**Verified:** `stage0_verify.py` 82/82, `verify_patched.py` 42/42 (unchanged
— it only checks the original 17), a fresh full sweep of all 25 branches +
2 string pointers in the built binary (every one now resolves to a named
vendor function or stays correctly intra-payload, zero unresolved), all four
existing emulation harnesses still PASS, and a **new, targeted regression
test** (`emulate_crash_20261108.py`) that starts emulation at the exact bug
site: against `corrected_v2` it reaches the correct target `0x895504`
(**PASS**); against the original `corrected` build it reaches the garbage
target `0x87b538` (**FAIL**) — confirming both the diagnosis and the fix.

**Credit:** found via a real-hardware test the user ran independently (not
executed or instructed by this analysis); the forensic disassembly and fix
were done afterward, read-only, against the already-crashed build.

**Status: closed for this specific crash.** Not yet re-tested on hardware;
see "What a maintainer still has to do" below.

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
