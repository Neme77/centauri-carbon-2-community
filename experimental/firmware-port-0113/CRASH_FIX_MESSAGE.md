# Draft correction — NOT sent, pending your approval

Two places need this: a comment on **issue #334** and a comment on **PR
#336** (same text works for both, or trim per context).

---

**Real-hardware crash found and fixed.**

A real-hardware test of the build referenced above (`elegoo_printer.patched`,
SHA-256 `2828a99a42993788bbe39675909d832ed67fe1f2ab432d1ae305bad2524f8f4a`)
crashed reproducibly: `SIGSEGV`, `pc = 0x0`, shortly after "RTC WAN streaming
enabled", in the `DeviceStatus`-worker thread.

**Root cause:** the "17 branches" list used to build the 500-byte LAN/WAN
coexistence payload was itself incomplete. The payload contains 6 more
branches and 2 absolute string-pointer loads that were never relocated and
still pointed at stale 02.01.01.07 addresses. One of them (payload offset
`0x11c`, right after a known-good `bl get_mode()`) jumped into unrelated code
whenever LAN/WAN mode was active — matching the crash exactly: the reported
`lr` is precisely that `bl`'s return address.

Found with a new read-only scanner (`scan_payload_branches.py`) that checks
every branch/absolute-address load in the payload against both stock
builds' symbol tables, not just the previously-known 17. Fixed in
`apply_patches.py`; re-verified with `stage0_verify.py` (82/82),
`verify_patched.py` (42/42), all four existing emulation harnesses, and a
new targeted regression test (`emulate_crash_20261108.py`) that reproduces
the exact crash target against the old build (FAIL, as expected) and
confirms the fix against the new one (PASS).

**Corrected hash:** `elegoo_printer.patched` →
`688b7bf8a27762d56b35d9738ea5b0c3d45580b402e122b0f77ee49dab1a8cfd` (GUI and
camera hashes unchanged). Full writeup in `OPEN_QUESTIONS.md`
(2026-11-08 entry) and `PATCH_SITES.md` (section 2b).

Still static- and emulation-verified only — **not yet re-tested on
hardware**. Will report back once it is.

---
