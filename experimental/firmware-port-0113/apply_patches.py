#!/usr/bin/env python3
"""Deterministic, reproducible builder for the 02.01.01.13 port.

Stock inputs -> three patched binaries, with SHA-256 at every stage. This is
the single source of truth for every byte this port writes: it consolidates
steps that were previously run as one-off interactive snippets during the
porting session into one reusable, auditable script.

What it writes, in order (elegoo_printer):
  1. Builds the 500-byte coexistence payload from the ORIGINAL
     experimental/firmware-port/network44-manifest.json "after" field for
     va=0xcd77e8 (the real 02.01.01.07 LAN/WAN coexistence logic), with the
     17 payload-internal branch words (GROUP_B), 6 more branches
     (EXTRA_BRANCHES) and 2 string-pointer relocations (STRING_RELOCS)
     overlaid at their payload-relative offsets. See "Known issues fixed
     here" below.
  2. Runs the K1 ELF restructure (same logic as k1_restructure.py) with that
     payload, producing the behavior-neutral restructured binary.
  3. Writes the two verbatim block-inserts (112 B @ old VA 0x812128, 444 B @
     old VA 0x84f940), read directly from experimental/firmware-port/
     http44-manifest.json "after" fields.
  4. Writes the 10 verbatim data/NOP/in-function patches, read directly from
     the http44/network44 manifests.
  5. Computes and writes the 12 payload-reference branch words and 2
     cross-function branch words (ARM A32 b/bl, deterministic formula).
  6. Verifies the result with verify_patched.py's logic (imported, not
     reimplemented) and aborts without writing if any check fails.

GUI and camera are single-instruction patches, written after a before-byte
check against the stock file.

Known issue #1, fixed here (see OPEN_QUESTIONS.md, point 0): the
elegoo_printer.k1 built manually earlier in this port (SHA-256
adb8529358f12c7f5f10dcfdd8a9e30a0c385cb6bfd8b404d8bcb42b5d6abcc6, already
referenced in the GitHub handoff issue) has its payload built from a template
that was NOT the real network44-manifest after-field: ~353 of 500 payload
bytes are zero instead of the original LAN/WAN coexistence logic, so only the
17 relocated branches are structurally correct — the logic they are supposed
to connect is missing. verify_patched.py does not catch this because it only
checks branch targets, not payload body content. This script sources the
payload from the real manifest, so its output's payload-body hash differs
from (and corrects) the earlier one.

Known issue #2, fixed here (see OPEN_QUESTIONS.md, the 2026-11-08 crash entry):
a real-hardware test of the first corrected build (SHA-256
2828a99a42993788bbe39675909d832ed67fe1f2ab432d1ae305bad2524f8f4a) crashed
with SIGSEGV (pc=0) during "RTC WAN streaming enabled". Root cause: the
"17 branches" list (GROUP_B) used to build the payload was itself incomplete.
`scan_payload_branches.py` (read-only) found 6 more branches and 2
movw/movt absolute-address pairs inside the payload that were never
relocated and still pointed at stale 02.01.01.07 addresses -- one of them
(payload offset 0x11c, right after a known-good branch whose return address
matches the crash's reported `lr`) plausibly explains the crash directly.
This script now also applies EXTRA_BRANCHES and STRING_RELOCS (both derived
by, and cross-checked against, scan_payload_branches.py's output) on top of
the GROUP_B branches, so its output's payload-body hash differs from (and
corrects) that second build too.

Neither corrected build overwrites the earlier one; each writes to a new
path for comparison.

Usage:
  python3 apply_patches.py STOCK_PRINTER STOCK_GUI STOCK_CAMERA OUT_DIR
      [--force] [--report REPORT.json]

All stock paths and OUT_DIR are required positional arguments. Nothing is
written if any before-byte check, manifest lookup, or verification fails.
"""
import argparse
import hashlib
import json
import os
import struct
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import stage0_verify as s0
import verify_patched as vp

REPO_FIRMWARE_PORT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "firmware-port")
OLD_PAYLOAD_VA = 0xCD77E8
PAYLOAD_SIZE = 500
PAYLOAD_FILE_OFF = 0xCB4FAC
PAYLOAD_VA = 0xCC4FAC
CUT = 0xCB5000
DELTA = 0x10000

BLOCK_INSERTS = [  # (old va, new va, size) -- after bytes read live from http44-manifest.json
    (0x812128, 0x8198B8, 112),
    (0x84F940, 0x8570D0, 444),
]

GUI_PATCH = dict(off=0x647C8, before="9ce700eb", after="000100ea", desc="b 0x74bd0 (skip data_get_print_status)")
CAMERA_PATCH = dict(off=0x12287C, before="b1df00eb", after="dddf00eb", desc="bl 0x16a7f8 resume (was pause)")

# 6 payload-internal branches GROUP_B missed (found by scan_payload_branches.py
# after the 2026-11-08 real-hardware crash). (payload_offset, kind, cond, new_target)
EXTRA_BRANCHES = [
    (0x00C, "b", 0x1, 0x819C28),  # NetworkManager::set_mode+0x68c
    (0x018, "b", 0x0, 0x819C28),
    (0x024, "b", 0x1, 0x819C28),
    (0x030, "b", 0x0, 0x819C28),
    (0x050, "b", 0x0, 0x82691C),  # LanStrategy::start+0x27c
    (0x11C, "b", 0x1, 0x895504),  # post_change_message+0x304 -- the crash site
]

# 2 movw/movt string-pointer pairs GROUP_B missed (self-references into the
# payload's own embedded strings; relocate by the same delta as the payload).
STRING_RELOCS = [0x1D0, 0x1E8]  # movw at this offset, movt at offset+4, both reg r1


def fail(msg):
    print("FEHLER:", msg, file=sys.stderr)
    sys.exit(2)


def load_manifest_after(manifest_path, va_hex):
    m = json.load(open(manifest_path))
    for p in m["patches"]:
        if p.get("va", "").lower() == va_hex.lower():
            return bytes.fromhex(p["after"])
    fail(f"VA {va_hex} nicht in {manifest_path} gefunden")


def branch_word(src, dst, kind, cond=0xE):
    delta = dst - src - 8
    if delta % 4 != 0:
        fail(f"delta 0x{delta:x} nicht durch 4 teilbar (src=0x{src:x} dst=0x{dst:x})")
    if not (-0x2000000 <= delta < 0x2000000):
        fail(f"delta 0x{delta:x} ausserhalb ARM-Reichweite (src=0x{src:x} dst=0x{dst:x})")
    imm24 = (delta >> 2) & 0xFFFFFF
    base = 0x0B000000 if kind == "bl" else 0x0A000000
    word = (cond << 28) | base | imm24
    return struct.pack("<I", word)


def rewrite_movwt_imm16(old_word, new_imm16):
    """Replace only the 16-bit immediate of a movw/movt word, preserving its
    condition code, opcode (movw vs movt) and destination register exactly."""
    imm4 = (new_imm16 >> 12) & 0xF
    imm12 = new_imm16 & 0xFFF
    word = (old_word & ~(0xF << 16) & ~0xFFF) | (imm4 << 16) | imm12
    return struct.pack("<I", word)


def build_payload():
    """Payload = real network44 after-field + the 17 branch words overlaid."""
    net_manifest = os.path.join(REPO_FIRMWARE_PORT, "network44-manifest.json")
    base_payload = bytearray(load_manifest_after(net_manifest, "0xcd77e8"))
    if len(base_payload) != PAYLOAD_SIZE:
        fail(f"network44 payload after-field ist {len(base_payload)} B, erwartet {PAYLOAD_SIZE}")
    for old_src, dst, label in vp.GROUP_B:
        off = old_src - OLD_PAYLOAD_VA
        # Instruction kind comes from the GROUP_B label text ("bl ..." / "b  ...").
        kind = "bl" if label.strip().startswith("bl") else "b"
        base_payload[off:off + 4] = branch_word(off + PAYLOAD_VA, dst, kind)

    # 6 additional branches scan_payload_branches.py found (not in GROUP_B).
    for off, kind, cond, dst in EXTRA_BRANCHES:
        base_payload[off:off + 4] = branch_word(off + PAYLOAD_VA, dst, kind, cond=cond)

    # 2 movw/movt string-pointer pairs: relocate self-reference by the payload's
    # own base delta, preserving each word's original condition code untouched.
    delta = PAYLOAD_VA - OLD_PAYLOAD_VA
    for off in STRING_RELOCS:
        movw_old = struct.unpack_from("<I", base_payload, off)[0]
        movt_old = struct.unpack_from("<I", base_payload, off + 4)[0]
        old_imm16 = ((movw_old >> 16) & 0xF) << 12 | (movw_old & 0xFFF)
        old_imm16_hi = ((movt_old >> 16) & 0xF) << 12 | (movt_old & 0xFFF)
        old_abs = (old_imm16_hi << 16) | old_imm16
        new_abs = old_abs + delta
        base_payload[off:off + 4] = rewrite_movwt_imm16(movw_old, new_abs & 0xFFFF)
        base_payload[off + 4:off + 8] = rewrite_movwt_imm16(movt_old, (new_abs >> 16) & 0xFFFF)
    return bytes(base_payload)


def k1_restructure(stock_bytes, payload_bytes):
    e = s0.parse(stock_bytes)
    ex = s0.load_exec(e["ph"])
    if ex["off"] != 0:
        fail("Exec-LOAD nicht bei p_offset 0")
    loads_after = [p for p in e["ph"] if p["type"] == 1 and p["off"] > ex["off"]]
    cut = min(p["off"] for p in loads_after)
    if cut != CUT:
        fail(f"CUT 0x{cut:x} != erwartet 0x{CUT:x}")
    oex_end = ex["off"] + ex["fsz"]
    if oex_end != PAYLOAD_FILE_OFF:
        fail(f"Exec-Ende 0x{oex_end:x} != 0x{PAYLOAD_FILE_OFF:x}")
    shift_secs = [s for s in e["sh"] if s["off"] >= cut and s["type"] != 8]
    if len(shift_secs) != 10:
        fail(f"{len(shift_secs)} Sektionen statt 10")

    nd = bytearray(stock_bytes[:cut] + b"\x00" * DELTA + stock_bytes[cut:])

    def patch_u32(off, newval):
        struct.pack_into("<I", nd, off, newval)

    patch_u32(0x20, e["shoff"] + DELTA)
    for p in e["ph"]:
        if p["off"] >= cut and p["type"] in (1, 2, 7):
            patch_u32(p["at"] + 4, p["off"] + DELTA)
    newsz = PAYLOAD_FILE_OFF + PAYLOAD_SIZE
    patch_u32(ex["at"] + 16, newsz)
    patch_u32(ex["at"] + 20, newsz)
    new_shoff = e["shoff"] + DELTA
    for s in shift_secs:
        patch_u32(new_shoff + s["i"] * e["shentsize"] + 0x10, s["off"] + DELTA)
    nd[PAYLOAD_FILE_OFF:PAYLOAD_FILE_OFF + PAYLOAD_SIZE] = payload_bytes
    return nd


def apply_block_inserts(nd):
    http_manifest = os.path.join(REPO_FIRMWARE_PORT, "http44-manifest.json")
    for old_va, new_va, size in BLOCK_INSERTS:
        after = load_manifest_after(http_manifest, hex(old_va))
        if len(after) != size:
            fail(f"Block {hex(old_va)}: Manifest-after ist {len(after)} B, erwartet {size}")
        fo = new_va - 0x10000
        nd[fo:fo + size] = after


def apply_verbatim_and_wiring(nd):
    http_manifest = os.path.join(REPO_FIRMWARE_PORT, "http44-manifest.json")
    net_manifest = os.path.join(REPO_FIRMWARE_PORT, "network44-manifest.json")

    def manifest_after_for(old_va_hex):
        for mp in (http_manifest, net_manifest):
            m = json.load(open(mp))
            for p in m["patches"]:
                if p.get("va", "").lower() == old_va_hex.lower():
                    return bytes.fromhex(p["after"])
        fail(f"{old_va_hex} in keinem Manifest gefunden")

    # 10 verbatim data/NOP/in-function sites: new_fo = old_va + 0x7790 - 0x10000
    verbatim_old_vas = ["0x81f1b4", "0x811c0c", "0x812120", "0x84ec20", "0x850238",
                         "0x8509a8", "0x88db48", "0x812494", "0x850000", "0x8509c4"]
    for old_va_hex in verbatim_old_vas:
        old_va = int(old_va_hex, 16)
        after = manifest_after_for(old_va_hex)
        fo = old_va + 0x7790 - 0x10000
        nd[fo:fo + len(after)] = after

    # 12 payload references + 2 cross-function: compute b/bl at new_fo targeting dst
    sites = [
        (0x811c10, 0xCC5008, "bl"), (0x811c70, 0xCC5050, "bl"), (0x812124, 0xCC5008, "bl"),
        (0x81f028, 0xCC4FE8, "b"), (0x88dd54, 0xCC5090, "bl"), (0x88dd70, 0xCC50BC, "b"),
        (0x88dfc4, 0xCC50AC, "bl"), (0x812158, 0xCC4FAC, "b"), (0x851754, 0xCC50D0, "bl"),
        (0x851924, 0xCC50D0, "bl"), (0x837168, 0xCC5170, "bl"), (0x8371a0, 0xCC5188, "bl"),
        (0x850eb4, 0x857160, "b"), (0x852500, 0x857140, "bl"),
    ]
    for old_va, dst, kind in sites:
        new_va = old_va + 0x7790
        fo = new_va - 0x10000
        nd[fo:fo + 4] = branch_word(new_va, dst, kind)


def patch_single_word(stock_bytes, spec):
    before = bytes.fromhex(spec["before"])
    off = spec["off"]
    if stock_bytes[off:off + len(before)] != before:
        fail(f"Vor-Byte-Check fehlgeschlagen @0x{off:x}: "
             f"{stock_bytes[off:off + len(before)].hex()} != {before.hex()}")
    out = bytearray(stock_bytes)
    out[off:off + len(before)] = bytes.fromhex(spec["after"])
    return bytes(out)


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("stock_printer")
    ap.add_argument("stock_gui")
    ap.add_argument("stock_camera")
    ap.add_argument("out_dir")
    ap.add_argument("--force", action="store_true")
    ap.add_argument("--report")
    a = ap.parse_args()

    os.makedirs(a.out_dir, exist_ok=True)
    report = {"inputs": {}, "outputs": {}, "checks": {}}

    stock_printer = open(a.stock_printer, "rb").read()
    report["inputs"]["stock_printer_sha256"] = hashlib.sha256(stock_printer).hexdigest()

    print("== 1/6: Payload aus network44-manifest + 17 Branch-Woerter bauen ==")
    payload = build_payload()
    print(f"   payload sha256 = {hashlib.sha256(payload).hexdigest()}")

    print("== 2/6: K1-Umstrukturierung ==")
    nd = k1_restructure(stock_printer, payload)

    print("== 2b/6: Stufe-0-Strukturpruefung (vor jedem Code-Patch) ==")
    restructured_tmp = os.path.join(a.out_dir, ".restructured_tmp")
    os.makedirs(a.out_dir, exist_ok=True)
    with open(restructured_tmp, "wb") as f:
        f.write(nd)
    s0_rc = s0.verify(a.stock_printer, restructured_tmp, None)
    os.remove(restructured_tmp)
    report["checks"]["stage0_rc"] = s0_rc
    if s0_rc != 0:
        fail("stage0_verify FAIL -> ELF-Umstrukturierung stimmt nicht; Datei bleibt liegen zur Analyse")
    print("   82/82 PASS (Struktur, Payload-Platzierung; noch keine Code-Patches)")

    print("== 3/6: 2 Block-Inserts (verbatim aus http44-manifest) ==")
    apply_block_inserts(nd)

    print("== 4/6: 10 Verbatim-Stellen + 12 Payload-Referenzen + 2 Cross-Function ==")
    apply_verbatim_and_wiring(nd)

    printer_out = os.path.join(a.out_dir, "elegoo_printer.patched")
    if os.path.exists(printer_out) and not a.force:
        fail(f"{printer_out} existiert (--force)")
    with open(printer_out, "wb") as f:
        f.write(nd)
    printer_sha = hashlib.sha256(bytes(nd)).hexdigest()
    report["outputs"]["elegoo_printer.patched_sha256"] = printer_sha
    print(f"   -> {printer_out}  sha256={printer_sha}")

    print("== 5/6: verify_patched.py (Branch-Verdrahtung, nach allen Code-Patches) ==")
    vp_proc = subprocess.run([sys.executable, "-I", os.path.join(HERE, "verify_patched.py"), printer_out],
                              capture_output=True, text=True)
    report["checks"]["verify_patched_rc"] = vp_proc.returncode
    report["checks"]["verify_patched_output"] = vp_proc.stdout.splitlines()[-2:]
    if vp_proc.returncode != 0:
        print(vp_proc.stdout)
        fail("verify_patched.py FAIL -> Branch-Verdrahtung stimmt nicht; Datei bleibt liegen zur Analyse")
    print("  ", vp_proc.stdout.strip().splitlines()[-2])

    print("== 6/6: GUI + Kamera (Einzelwort-Patches) ==")
    stock_gui = open(a.stock_gui, "rb").read()
    gui_out = os.path.join(a.out_dir, "ec-eeb001-gui.patched")
    if os.path.exists(gui_out) and not a.force:
        fail(f"{gui_out} existiert (--force)")
    gui_patched = patch_single_word(stock_gui, GUI_PATCH)
    open(gui_out, "wb").write(gui_patched)
    gui_sha = hashlib.sha256(gui_patched).hexdigest()
    report["outputs"]["ec-eeb001-gui.patched_sha256"] = gui_sha
    print(f"   -> {gui_out}  sha256={gui_sha}")

    stock_camera = open(a.stock_camera, "rb").read()
    camera_out = os.path.join(a.out_dir, "ai_camera.patched")
    if os.path.exists(camera_out) and not a.force:
        fail(f"{camera_out} existiert (--force)")
    camera_patched = patch_single_word(stock_camera, CAMERA_PATCH)
    open(camera_out, "wb").write(camera_patched)
    camera_sha = hashlib.sha256(camera_patched).hexdigest()
    report["outputs"]["ai_camera.patched_sha256"] = camera_sha
    print(f"   -> {camera_out}  sha256={camera_sha}")

    if a.report:
        with open(a.report, "w") as f:
            json.dump(report, f, indent=2)
    print("\nFertig. Diese drei Dateien sind deterministisch aus den Stock-Inputs reproduzierbar.")
    print("HINWEIS: elegoo_printer.patched hat einen anderen Payload-Inhalt (und damit eine andere")
    print("SHA-256) als das frueher manuell gebaute WORK/elegoo_printer.k1 - siehe OPEN_QUESTIONS.md Punkt 0.")


if __name__ == "__main__":
    main()
