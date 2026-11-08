#!/usr/bin/env python3
"""Read-only scanner: find EVERY branch and absolute-address load inside the
500-byte LAN/WAN coexistence payload, not just the previously hand-picked 17
branches (GROUP_B in verify_patched.py / apply_patches.py).

Background: a real-hardware crash (SIGSEGV, pc=0, during "RTC WAN streaming
enabled") traced to payload offset 0x11c jumping to a stale, unrelocated
target. This script systematically re-derives the complete, correct table so
apply_patches.py can be extended once, instead of relying on another manual
forensic pass next time.

Method, for every 4-byte word in the OLD (02.01.01.07) payload body (read
straight from experimental/firmware-port/network44-manifest.json's "after"
field for va=0xcd77e8 -- the untouched original logic, before any of this
port's branch fixups were applied):

  - Skip words that look like string-literal data (all bytes printable ASCII
    or NUL), not instructions.
  - Decode B/BL (incl. conditional). If the target falls OUTSIDE the OLD
    payload's own address range, it references a vendor function: resolve it
    via `nm -C` on the OLD stock elegoo_printer (nearest symbol <= target),
    then find the SAME symbol name in the NEW stock elegoo_printer and derive
    the new target = new_symbol_start + (old_target - old_symbol_start).
    Compare against GROUP_B's already-known 17 to report only the NEW finds.
  - Decode movw/movt PAIRS (same destination register, consecutive or near
    words) forming an absolute 32-bit value. If that value falls inside the
    OLD payload's own address range, it's a self-reference (relocate to NEW
    payload base + same relative offset). Otherwise resolve the same way as
    branches, using `nm -C`'s data symbols too.

Nothing is written. Only reads network44-manifest.json and the two stock
elegoo_printer binaries (paths given on the command line).

Usage:
  python3 scan_payload_branches.py OLD_STOCK_elegoo_printer NEW_STOCK_elegoo_printer
      [--report REPORT.json]
"""
import argparse
import bisect
import json
import os
import struct
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import verify_patched as vp

NET_MANIFEST = os.path.join(HERE, "..", "firmware-port", "network44-manifest.json")
OLD_PAYLOAD_VA = 0xCD77E8
PAYLOAD_SIZE = 500
NEW_PAYLOAD_VA = 0xCC4FAC

KNOWN_OFFSETS = {old_src - OLD_PAYLOAD_VA for old_src, _, _ in vp.GROUP_B}


def all_syms(path):
    """Return sorted (addr, name, kind) for every symbol nm -C reports, text or data."""
    out = subprocess.run(["nm", "-C", path], capture_output=True, text=True)
    if out.returncode != 0 or not out.stdout.strip():
        sys.exit(f"FEHLER: 'nm' lieferte nichts fuer {path}")
    rows = []
    for ln in out.stdout.splitlines():
        p = ln.split(" ", 2)
        if len(p) == 3 and p[1].strip() not in ("U",):
            try:
                rows.append((int(p[0], 16), p[2], p[1].strip()))
            except ValueError:
                pass
    rows.sort()
    return rows


def nearest_symbol(sorted_syms, addr):
    keys = [s[0] for s in sorted_syms]
    i = bisect.bisect_right(keys, addr) - 1
    if i < 0:
        return None
    return sorted_syms[i]  # (addr, name, kind)


def resolve_by_name(sorted_syms_by_addr, name_index, name, old_off):
    cands = name_index.get(name, [])
    if len(cands) == 1:
        return cands[0] + old_off, "eindeutig"
    if len(cands) == 0:
        return None, "Name nicht im neuen Build gefunden"
    return None, f"{len(cands)} gleichnamige Symbole im neuen Build, nicht eindeutig"


def is_probably_string_data(word_bytes):
    return all(b == 0 or 0x20 <= b <= 0x7E for b in word_bytes)


def decode_branch(word, addr):
    if (word >> 25) & 7 != 5:
        return None
    imm = word & 0xFFFFFF
    if imm & 0x800000:
        imm -= 1 << 24
    kind = "bl" if (word >> 24) & 1 else "b"
    cond = word >> 28
    return kind, cond, addr + 8 + (imm << 2)


def decode_movw_movt(word):
    # ARM A32: movw cccc 0011 0000 iiii dddd iiiiiiiiiiii ; movt cccc 0011 0100 ...
    if (word & 0x0FF00000) == 0x03000000:
        rd = (word >> 12) & 0xF
        imm = ((word >> 16) & 0xF) << 12 | (word & 0xFFF)
        return "movw", rd, imm
    if (word & 0x0FF00000) == 0x03400000:
        rd = (word >> 12) & 0xF
        imm = ((word >> 16) & 0xF) << 12 | (word & 0xFFF)
        return "movt", rd, imm
    return None


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("old_stock")
    ap.add_argument("new_stock")
    ap.add_argument("--report")
    a = ap.parse_args()

    m = json.load(open(NET_MANIFEST))
    payload = None
    for p in m["patches"]:
        if p.get("va", "").lower() == "0xcd77e8":
            payload = bytes.fromhex(p["after"])
    if payload is None or len(payload) != PAYLOAD_SIZE:
        sys.exit("FEHLER: Payload nicht in network44-manifest.json gefunden/falsche Groesse")

    old_syms = all_syms(a.old_stock)
    new_syms = all_syms(a.new_stock)
    new_name_index = {}
    for addr, name, kind in new_syms:
        new_name_index.setdefault(name, []).append(addr)

    findings_branches = []
    findings_movw = []

    i = 0
    while i < PAYLOAD_SIZE - 3:
        word = struct.unpack_from("<I", payload, i)[0]
        wbytes = payload[i:i + 4]
        if is_probably_string_data(wbytes):
            i += 4
            continue
        br = decode_branch(word, OLD_PAYLOAD_VA + i)
        if br:
            kind, cond, target = br
            if OLD_PAYLOAD_VA <= target < OLD_PAYLOAD_VA + PAYLOAD_SIZE:
                i += 4
                continue  # intra-payload, relocation-safe, not interesting
            sym = nearest_symbol(old_syms, target)
            known = i in KNOWN_OFFSETS
            entry = dict(payload_off=hex(i), old_va=hex(OLD_PAYLOAD_VA + i), kind=kind, cond=cond,
                         old_target=hex(target), known_in_group_b=known)
            if sym:
                sym_addr, sym_name, sym_kind = sym
                off = target - sym_addr
                entry["old_symbol"] = sym_name
                entry["old_symbol_off"] = hex(off)
                new_addr, note = resolve_by_name(new_syms, new_name_index, sym_name, off)
                entry["new_target"] = hex(new_addr) if new_addr is not None else None
                entry["resolution"] = note
            else:
                entry["old_symbol"] = None
                entry["resolution"] = "kein Symbol <= target im alten Build"
            if not known:
                findings_branches.append(entry)
            i += 4
            continue
        mv = decode_movw_movt(word)
        if mv and i + 4 < PAYLOAD_SIZE:
            kind, rd, imm_lo = mv
            if kind == "movw":
                word2 = struct.unpack_from("<I", payload, i + 4)[0]
                mv2 = decode_movw_movt(word2)
                if mv2 and mv2[0] == "movt" and mv2[1] == rd:
                    abs32 = (mv2[2] << 16) | imm_lo
                    entry = dict(payload_off=hex(i), old_va=hex(OLD_PAYLOAD_VA + i), reg=f"r{rd}", abs_value=hex(abs32))
                    if OLD_PAYLOAD_VA <= abs32 < OLD_PAYLOAD_VA + PAYLOAD_SIZE:
                        rel = abs32 - OLD_PAYLOAD_VA
                        entry["classification"] = "self-reference (points inside the payload itself)"
                        entry["new_target"] = hex(NEW_PAYLOAD_VA + rel)
                    else:
                        sym = nearest_symbol(old_syms, abs32)
                        if sym:
                            sym_addr, sym_name, sym_kind = sym
                            off = abs32 - sym_addr
                            new_addr, note = resolve_by_name(new_syms, new_name_index, sym_name, off)
                            entry["classification"] = f"vendor reference: {sym_name}+0x{off:x} ({sym_kind})"
                            entry["new_target"] = hex(new_addr) if new_addr is not None else None
                            entry["resolution"] = note
                        else:
                            entry["classification"] = "unresolved absolute value, no symbol found"
                    findings_movw.append(entry)
                    i += 8
                    continue
        i += 4

    print(f"OLD payload: {a.old_stock}")
    print(f"NEW payload: {a.new_stock}")
    print(f"Bereits bekannte (GROUP_B) Offsets: {sorted(hex(o) for o in KNOWN_OFFSETS)}\n")

    print(f"=== NEU GEFUNDENE, NICHT-RELOZIERTE BRANCHES ({len(findings_branches)}) ===")
    for e in findings_branches:
        print(f"  off {e['payload_off']:>6}  {e['kind']:3} cond={e['cond']:x}  old_target={e['old_target']}  "
              f"-> {e.get('old_symbol')}+{e.get('old_symbol_off')}  NEW TARGET = {e.get('new_target')}  "
              f"({e.get('resolution')})")

    print(f"\n=== movw/movt ABSOLUTE-ADDRESS PAARE ({len(findings_movw)}) ===")
    for e in findings_movw:
        print(f"  off {e['payload_off']:>6}  {e['reg']}  value={e['abs_value']}  {e['classification']}  "
              f"NEW TARGET = {e.get('new_target')}  ({e.get('resolution', '')})")

    if a.report:
        json.dump({"branches": findings_branches, "movw_movt": findings_movw},
                   open(a.report, "w"), indent=2)

    unresolved = [e for e in findings_branches + findings_movw if e.get("new_target") is None]
    if unresolved:
        print(f"\n{len(unresolved)} Stelle(n) konnten NICHT eindeutig aufgeloest werden -- Handarbeit noetig.")
        return 1
    print(f"\nAlle gefundenen Stellen eindeutig aufgeloest.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
