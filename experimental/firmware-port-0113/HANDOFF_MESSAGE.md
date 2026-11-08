# Draft message to Neme77 — NOT sent, pending your approval

Copy-paste draft for a correction comment on issue #334 (or a new issue). Not
posted anywhere by Claude; GitHub push is disabled on `origin` and there is
no authenticated `gh` session in this environment either way, so posting is
yours to do regardless.

---

**Correction to my earlier message above.**

While preparing a fuller handoff package I found a defect in the
`elegoo_printer` binary I described: its 500-byte LAN/WAN coexistence
payload was built from a template that left most of the original
02.01.01.07 logic zeroed out — only the 17 relocated branch instructions
were actually present, not the code connecting them. My earlier
verification (42/42 PASS) checked branch wiring, not payload content, so it
didn't catch this.

**Corrected hashes** (same patches, payload rebuilt from the real
network44-manifest logic):
- `elegoo_printer.patched`: `2828a99a42993788bbe39675909d832ed67fe1f2ab432d1ae305bad2524f8f4a`
  (was `adb8529358f12c7f5f10dcfdd8a9e30a0c385cb6bfd8b404d8bcb42b5d6abcc6`)
- `ec-eeb001-gui.patched` and `ai_camera.patched`: unchanged, same hashes as before.

Re-verified against the corrected binary: `stage0_verify.py` 82/82,
`verify_patched.py` 42/42, and all four emulation harnesses (GUI Z-offset,
camera coexistence, HTTP upload guards ×32 states, LanStrategy stop) still
PASS.

I've also added a single reproducible script (`apply_patches.py`) that
builds all three patched binaries from the stock inputs in one deterministic
pass, with per-site before/after bytes in `PATCH_SITES.md` and the open
points — including the one payload branch target that's structurally
inferred rather than fully disassembled, and the fact that none of this is
hardware-tested — in `OPEN_QUESTIONS.md`.

Opened the draft PR you asked for: **#336**. It has the exact base commit,
per-site patch tables, the deterministic script, the emulation harnesses,
and the reproduce commands — source and scripts only, no vendor binaries or
signing material. The `LanStrategy::start+0x11c` branch target is flagged
there as still inferred, as requested.

Still asking the same thing: **please take over build + signing + hardware
test** when you have time — the builder stays fail-closed for this base
until then, correctly. Happy to answer anything about the scripts, offsets,
or emulation results in the meantime.

---
