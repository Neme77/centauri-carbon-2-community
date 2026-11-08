# Handoff package checklist

What to include, in what order, and what to explicitly leave out.

## 1. Correct the existing GitHub issue first

The issue already posted references `elegoo_printer.k1`
(`adb8529358f12c7f5f10dcfdd8a9e30a0c385cb6bfd8b404d8bcb42b5d6abcc6`), which
`OPEN_QUESTIONS.md` point 0 shows has a defective payload body. Before
sending anything else: either edit that issue or add a correction comment
pointing at the corrected hash
(`2828a99a42993788bbe39675909d832ed67fe1f2ab432d1ae305bad2524f8f4a`) and
explaining why. Sending new material without correcting this first would
leave two contradictory claims live at once.

## 2. Documents to attach/link (all in `experimental/firmware-port-0113/`)

| File | Purpose |
|---|---|
| `README_PORT.md` | Entry point: what was ported, script order, SHA-256 checklist |
| `PATCH_SITES.md` | Every patch site: file, offset, before/after, purpose |
| `VERIFY_AND_EMULATE.md` | How to run verification/emulation, what results mean and don't mean |
| `OPEN_QUESTIONS.md` | The 4 open points, concrete tests, priority |
| `apply_patches.py` | Reproducibility script: stock inputs → 3 patched binaries, with verification |
| `k1_restructure.py`, `stage0_verify.py`, `verify_patched.py` | Underlying restructure/verification tools `apply_patches.py` calls |
| `emulate_e1.py`, `emulate_e2.py`, `emulate_e345.py`, `emulate_e6.py` | Emulation harnesses |
| `VERIFY_OUTPUT.txt`, `EMULATE_E1_OUTPUT.txt`, `EMULATE_E2_OUTPUT.txt`, `EMULATE_E345_OUTPUT.txt`, `EMULATE_E6_OUTPUT.txt` | Captured evidence of the above (re-run against the corrected binary) |
| Existing analysis docs (`README.md`, `DIFF_BLOCKS.md`, `PORTING_METHODOLOGY.md`, `PAYLOAD_BASE_FIXATION_AND_BRANCH_PLAN.md`, `K1_FEASIBILITY.md`, `K1_RESTRUCTURE_SPEC.md`, `TEST_MATRIX.md`, `RISK_LIST.md`, `COMPATIBILITY_AND_ROLLBACK.md`, `EMULATION_PLAN.md`, `HARDWARE_TEST_*.md`, `ROLLBACK_PLAN.md`) | Background, already referenced from the above |

## 3. What NOT to include

- **No private signing keys, no AES keys.** Not present in this directory;
  confirm before sending anything from `builder/current/keys/`.
- **No signed `.swu.sig` / `.zip.sig` packages.** Building and signing a
  flashable package is explicitly the maintainer's step (needs the private
  keys this package does not and should not contain).
- **No printer LAN access codes, no local network IPs, no passwords.** None
  are referenced in the files above; double-check any copy-pasted terminal
  output before attaching it (see §4).
- **The stock vendor binaries themselves are not attached** — they are
  ELEGOO's firmware, not redistributed here. Only their SHA-256 hashes are
  given, so the maintainer can confirm they're using the same inputs.
- **`elegoo_printer.k1` (the defective intermediate) is not re-sent** as a
  "final" artifact — only referenced in `OPEN_QUESTIONS.md` as superseded.

## 4. Order to assemble

1. Correct/update the GitHub issue (§1).
2. Attach or link the files in §2.
3. Run the pre-send checklist below.
4. Post the final message (`HANDOFF_MESSAGE.md`).

## 5. Pre-send checklist (final pass before anything goes out)

- [ ] Ran `apply_patches.py` once more, right before sending, and confirmed
  the three output hashes still match the ones quoted in `README_PORT.md`.
- [ ] Grepped every file about to be attached for `MTY4ODE2`, `192.168.`,
  `root@`, or any string that looks like a password, access code, or private
  IP — none should appear (this port's docs don't reference the printer's
  own network, only vendor binaries).
- [ ] Confirmed no file under `builder/current/keys/` is attached.
- [ ] Re-read `OPEN_QUESTIONS.md` point 0 one more time and confirmed the
  message text below does not imply the port is "done" or "ready" —
  it is statically verified and emulation-tested, not hardware-tested.
- [ ] Decided whether to edit the existing issue in place or post a
  correction comment, and have that text ready before posting the rest.
