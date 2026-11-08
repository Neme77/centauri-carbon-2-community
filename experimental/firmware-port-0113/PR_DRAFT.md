# Draft PR — NOT opened, pending your approval

Target: `Neme77/centauri-carbon-2-community`, base `develop` (or the branch
Neme77's own `.13` port is on, if they'd rather diff against that — ask).
Source: your fork, branch `update-2.03.03.13`. Opened as a **draft PR** per
their request. Source + scripts only — no vendor binaries, no signing
material (confirmed: 0 files from `keys/` or `WORK/` in this commit).

---

**Title:**
```
[Draft] Experimental port of community patches to base 02.01.01.13 — tooling + docs, no binaries
```

**Description:**

```markdown
Draft PR for comparison with the other in-progress .13 port — see issue #334.
Source and scripts only, as requested. No vendor binaries, no signing material.

## Base
- Base commit: 47efbac0 (develop, PR #321)
- This branch: 59694b3b

## What's here (`experimental/firmware-port-0113/`)
- `PATCH_SITES.md` — every patch site: file, offset, before/after bytes, purpose
- `apply_patches.py` — single deterministic script: stock inputs → three
  patched binaries (elegoo_printer, ec-eeb001-gui, ai_camera), with
  `stage0_verify.py` + `verify_patched.py` run automatically before anything
  is written
- `emulate_e1.py`, `emulate_e2.py`, `emulate_e345.py`, `emulate_e6.py` —
  Unicorn-based emulation harnesses (GUI Z-offset, camera coexistence, HTTP
  upload guards × 32 states, LanStrategy stop)
- `README_PORT.md` / `VERIFY_AND_EMULATE.md` — reproduction commands and what
  each result does/doesn't prove
- `OPEN_QUESTIONS.md` — open points, most relevantly:
  **`LanStrategy::start+0x11c`'s branch target (`0x8267bc`) is structurally
  inferred (byte-identical target instruction, relocated neighbours), not
  confirmed by full disassembly** — flagged, not resolved, in this PR.

## Reproduce
\`\`\`sh
cd experimental/firmware-port-0113
python3 -I apply_patches.py STOCK_PRINTER STOCK_GUI STOCK_CAMERA OUT_DIR --report OUT_DIR/report.json
python3 -I verify_patched.py OUT_DIR/elegoo_printer.patched
python3 emulate_e1.py OUT_DIR/ec-eeb001-gui.patched
python3 emulate_e2.py OUT_DIR/ai_camera.patched
python3 emulate_e345.py OUT_DIR/elegoo_printer.patched
python3 emulate_e6.py OUT_DIR/elegoo_printer.patched
\`\`\`
Stock inputs are not included (vendor firmware); SHA-256 of the expected
inputs/outputs are in `README_PORT.md`.

## Status
Static verification + emulation only. Not hardware-tested.
`ported_patch_set` untouched (stays `false`). No request to change that here.

## Note on the earlier hash mismatch
An earlier manually-built `elegoo_printer.k1` (referenced in issue #334,
sha256 `adb85293...`) had a payload-body defect (see `OPEN_QUESTIONS.md`
point 0) — fixed in what's here. If your own `.13` port's fingerprint also
differs from this PR's output, that's worth comparing directly rather than
assuming either side is wrong — could be a different payload placement or
build pipeline producing an equally valid result.
```

---

## Still to decide before this goes live
- [ ] Confirm base branch: `develop`, or do they want it against their own
  in-progress `.13` branch?
- [ ] Confirm fork exists under `lpgamerzockt-ops` (see chat).
- [ ] You review/edit the title + description above.
- [ ] Only then: push branch to fork, `gh pr create --draft` with this text.
