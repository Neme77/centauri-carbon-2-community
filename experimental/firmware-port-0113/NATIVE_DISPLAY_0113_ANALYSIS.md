# Native display on 02.01.01.13 — scoping analysis (not a port)

Scopes the `native_gui` blocker left open in
`base_profiles/02.01.01.13.json` (`port.outputs.native_gui =
TODO_NATIVE_DISPLAY_0113`). Analysis only; no 0113 native-display output
is produced or committed here.

## What native display is

`experimental/native-display/patch.py` `apply()` takes the **Z-offset-
patched** GUI (`ec-eeb001-gui`), expands an RX ELF segment
(`elf_layout.expand_rx`), then applies **74 byte-exact, before-hashed
patch spans** from `patch-plan.json`, ending at a fixed `output_sha256`.
`package.py` additionally recolors the stock theme PNGs per
`recolor-plan.json`. So the release GUI is not the Z-offset GUI but a
UI-reskinned build layered on top of it.

For 02.01.01.07:
- input `base_sha256` = `5f3122…` (07 Z-offset GUI), `base_size` 2655456
- output `output_sha256` = `db09fa37…` (07 native GUI), `output_size` 2720992
- growth = **+0x10000** (65536 B), a segment expansion

## Structure of the 74 changes (measured from patch-plan.json)

- Total patched bytes: **18264**.
- **2 large spans** carry ~98 %: `0x2107c0` (15180 B) and `0x214310`
  (2680 B) — the embedded presentation payload (recolored icon/display
  data; `display_payload.S` is 205 KB of source).
- **72 small spans, 8 B each.** Decoding their replacement words shows the
  same three kinds this project already ports elsewhere:
  - ELF **structural offset fixups** for the +0x10000 expansion (e.g. the
    span at file `0xa4` writes `0x00214d88`, a program-header offset),
    exactly what `k1_restructure.py` does for the printer;
  - **`movw`/`movt` address-immediate** rewrites (top byte `0xe3`), which
    move when GUI addresses shift — the same class as the reactor's
    address patches;
  - a few **`B`/`BL` branch redirects** (top byte `0xea`/`0xeb`).

So native display is a reactor-scale address/offset remap plus two payload
blocks and a +0x10000 restructure — **the tooling and methodology already
exist** (`elf_layout.expand_rx`, the `k1_restructure` offset-shift logic,
the symbol-match address mapping from the reactor port).

## Port approach for 0113

1. Reconstruct the 0113 stock GUI (verified: reversing the Z-offset word
   at `0x647C8` on the confirmed patched GUI `668ab8cb…` yields
   `8d6cac2a…`, the profile `stock_gui` pin).
2. The native base input is the 0113 Z-offset GUI `668ab8cb…` (already
   confirmed and on the test device).
3. Re-derive the 74 spans against `668ab8cb…`:
   - structural offset fixups recompute from the 0113 ELF layout (same
     +0x10000 expansion);
   - address-immediate and branch spans remap by the 0113 GUI's own symbol
     addresses (both GUIs are unstripped, ~4443 named functions);
   - the two payload blocks are re-placed in the expanded segment; any
     absolute addresses inside them are fixed to the 0113 GUI.
4. Recolor the 0113 stock theme PNGs via `recolor-plan.json`.
5. The result becomes the new `native_gui` pin.

## Inputs still required

- **0113 stock theme images** — `/opt/inst/images/theme_default` on the
  device (plain PNG assets, no secret). Pull with:
  `ssh root@<printer> 'tar czf - /opt/inst/images/theme_default' > theme.tar.gz`
- **The 07 native-display GUI** (`5f3122…` input / `db09fa37…` output), or
  both GUIs' symbol maps, to read each span's semantic target and to
  validate the remap. Not present locally.

## Hard constraints (why this is the riskiest 0113 component)

- **No reference output hash.** Printer/GUI/camera had confirmed output
  pins to reproduce; the 0113 native GUI has none — correctness can only
  be argued from the remap, not checked against a known-good hash.
- **No hardware validation.** The test device ran the plain Z-offset GUI
  `668ab8cb…`, never a native-display GUI. So a 0113 native build is
  unvalidated until it is hardware-tested on its own.
- It changes the on-screen UI, so a wrong remap is user-visible, not
  silently fail-closed.

## Measured 2026-11-10: the 0113 stock theme images are entirely different

The 0113 stock theme images were pulled from the device
(`/opt/inst/images/theme_default`, 1112 PNGs). Compared against the
480 image entries in `baseline-pins.txt`: **0 match, 480 differ.** So the
image/theme side changed wholesale between 07 and 0113. The recolor side
(50 images in `recolor-plan.json`) therefore also needs full
re-derivation against the 0113 stock inputs, and `baseline-pins.txt` must
be regenerated for 0113 — it is not only the GUI ELF that moved.

## The wall: no reference and no 07 native GUI

To produce a **verifiable** `native_gui` the port needs a reference the
project does not have here:
- the **07 native-display GUI** (`5f3122…` input / `db09fa37…` output), or
  a symbol map of it, to read what each of the 74 spans targets — the
  spans' after-bytes encode 07 addresses and carry no semantic labels, so
  without the 07 GUI the remap cannot be derived reliably;
- a **reference output hash** for the 0113 native GUI — none exists, so
  the result cannot be checked against anything;
- a **hardware test** — native display never ran on the test device.

Without the 07 native GUI, the 74-span GUI remap is not reliably doable
from the patch-plan alone, and any result would be unvalidated on all
three counts above.

## Recommendation

Native display is a full re-port with no reference output and no prior
hardware validation. A lower-risk path to a buildable 0113 release is to
ship the validated surface first (the four patched binaries, as tested)
and treat native display as its own, separately hardware-tested port —
i.e. have the 0113 profile declare native display not-included for now
(design draft §3.5) rather than block the whole release on it. If full
parity is required, the port above is tractable with the existing
tooling, but must end in a hardware test before release.
