#!/usr/bin/env python3
"""Compare two elegoo_printer (or ec-eeb001-gui / ai_camera) ELF binaries,
function by function, to help compare two independent patch approaches.

Written for @Neme77's note on issue #334 that their own patched
elegoo_printer has a different fingerprint than ours — this makes that
comparison concrete instead of just a hash mismatch: for every named
function present in BOTH binaries (both builds are not stripped), it
reports whether the function moved by a consistent shift, kept its size,
and whether its bytes are identical, size-matched-but-different (both sides
patched it, possibly differently), or size-mismatched (recompiled /
restructured on at least one side).

READ-ONLY: opens files with 'rb' only; writes nothing, patches nothing.

Usage:
  python3 compare_patched_binaries.py BINARY_A BINARY_B
      [--only-diffs] [--function NAME] [--report REPORT.json]

  BINARY_A, BINARY_B: any two same-architecture ELFs with a symbol table
  (e.g. our WORK/corrected/elegoo_printer.patched vs. your own patched
  elegoo_printer, or either vs. the stock binary to see what changed).
"""
import argparse
import bisect
import hashlib
import json
import struct
import subprocess
import sys


def syms(path):
    out = subprocess.run(["nm", "-C", path], capture_output=True, text=True)
    if out.returncode != 0 or not out.stdout.strip():
        sys.exit(f"FEHLER: 'nm' lieferte keine Symbole fuer {path} "
                  f"(Datei fehlt, falsches Format, oder stripped binary ohne .symtab)")
    S = []
    for ln in out.stdout.splitlines():
        p = ln.split(" ", 2)
        if len(p) == 3 and p[1] in "tTwW":
            try:
                S.append((int(p[0], 16), p[2]))
            except ValueError:
                pass
    S.sort()
    return S


def by_name(S):
    d = {}
    for addr, name in S:
        d.setdefault(name, []).append(addr)
    return d


def elf_file_offset(path_data, va):
    """VA -> file offset via PT_LOAD segments (ELF32-LE)."""
    e_phoff, = struct.unpack_from("<I", path_data, 0x1C)
    phentsize, phnum = struct.unpack_from("<HH", path_data, 0x2A)
    for i in range(phnum):
        b = e_phoff + i * phentsize
        p_type, p_offset, p_vaddr, _, p_filesz = struct.unpack_from("<5I", path_data, b)
        if p_type == 1 and p_vaddr <= va < p_vaddr + p_filesz:
            return p_offset + (va - p_vaddr)
    return None


def func_size(starts, addr, i):
    return (starts[i + 1] - addr) if i + 1 < len(starts) else None


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("binary_a")
    ap.add_argument("binary_b")
    ap.add_argument("--only-diffs", action="store_true", help="skip rows where both are byte-identical")
    ap.add_argument("--function", help="only compare one function (by exact nm name)")
    ap.add_argument("--report")
    a = ap.parse_args()

    da = open(a.binary_a, "rb").read()
    db = open(a.binary_b, "rb").read()
    Sa, Sb = syms(a.binary_a), syms(a.binary_b)
    starts_a = [s[0] for s in Sa]
    starts_b = [s[0] for s in Sb]
    byname_a, byname_b = by_name(Sa), by_name(Sb)

    common = sorted(set(byname_a) & set(byname_b))
    if a.function:
        common = [n for n in common if n == a.function]
        if not common:
            sys.exit(f"'{a.function}' nicht in beiden Dateien eindeutig gefunden")

    rows = []
    n_identical_total = 0
    for name in common:
        addrs_a, addrs_b = byname_a[name], byname_b[name]
        if len(addrs_a) != 1 or len(addrs_b) != 1:
            rows.append(dict(name=name, status="AMBIGUOUS",
                              detail=f"{len(addrs_a)} Stelle(n) in A, {len(addrs_b)} in B"))
            continue
        va_a, va_b = addrs_a[0], addrs_b[0]
        i = bisect.bisect_left(starts_a, va_a)
        size_a = func_size(starts_a, va_a, i)
        j = bisect.bisect_left(starts_b, va_b)
        size_b = func_size(starts_b, va_b, j)
        if size_a is None or size_b is None:
            rows.append(dict(name=name, status="SKIP", detail="letzte Funktion, Groesse unbekannt"))
            continue
        shift = va_b - va_a
        fo_a, fo_b = elf_file_offset(da, va_a), elf_file_offset(db, va_b)
        if fo_a is None or fo_b is None:
            rows.append(dict(name=name, status="SKIP", detail="VA nicht in PT_LOAD"))
            continue
        if size_a != size_b:
            rows.append(dict(name=name, status="SIZE_DIFFERS", va_a=hex(va_a), va_b=hex(va_b),
                              size_a=size_a, size_b=size_b, shift=hex(shift),
                              detail="unterschiedliche Groesse -> wahrscheinlich anders rekompiliert "
                                     "oder anders gepatcht; Bytes nicht direkt vergleichbar"))
            continue
        ba = da[fo_a:fo_a + size_a]
        bb = db[fo_b:fo_b + size_a]
        identical = (ba == bb)
        n_identical_total += identical
        status = "IDENTICAL" if identical else "SAME_SIZE_DIFFERENT_BYTES"
        row = dict(name=name, status=status, va_a=hex(va_a), va_b=hex(va_b),
                   size=size_a, shift=hex(shift),
                   sha256_a=hashlib.sha256(ba).hexdigest()[:16],
                   sha256_b=hashlib.sha256(bb).hexdigest()[:16])
        if not identical:
            diffs = [k for k in range(len(ba)) if ba[k] != bb[k]]
            row["diff_byte_count"] = len(diffs)
            row["first_diff_offset"] = hex(diffs[0]) if diffs else None
        rows.append(row)
        if a.only_diffs and identical:
            rows.pop()

    only_a = sorted(set(byname_a) - set(byname_b))
    only_b = sorted(set(byname_b) - set(byname_a))

    print(f"A = {a.binary_a}\nB = {a.binary_b}")
    print(f"Gemeinsame benannte Funktionen: {len(common)}  |  nur in A: {len(only_a)}  |  nur in B: {len(only_b)}\n")
    for r in rows:
        print(f"[{r['status']:26}] {r['name'][:70]:70}", end="")
        if "shift" in r:
            print(f"  shift={r['shift']:>8}  size={r.get('size', r.get('size_a'))}", end="")
        if r["status"] == "SAME_SIZE_DIFFERENT_BYTES":
            print(f"  diff_bytes={r['diff_byte_count']} first_diff=+{r['first_diff_offset']}", end="")
        print()
    nD = sum(1 for r in rows if r["status"] == "SAME_SIZE_DIFFERENT_BYTES")
    nS = sum(1 for r in rows if r["status"] == "SIZE_DIFFERS")
    print(f"\n{n_identical_total} identical"
          + (" (hidden by --only-diffs)" if a.only_diffs else "")
          + f", {nD} same-size-but-different (both patched, compare these), "
          f"{nS} size-differs (recompiled/restructured differently)")
    if only_a:
        print(f"\nOnly in A ({len(only_a)}): {', '.join(only_a[:15])}" + (" ..." if len(only_a) > 15 else ""))
    if only_b:
        print(f"Only in B ({len(only_b)}): {', '.join(only_b[:15])}" + (" ..." if len(only_b) > 15 else ""))
    print("\nCAVEAT: function size is estimated as \"next symbol's start minus this one\'s\",")
    print("which overestimates near sparse/trailing symbols (e.g. _fini) with no next symbol")
    print("nearby -- ignore SIZE_DIFFERS/byte-diff rows for those, they are a measurement")
    print("artifact, not a real difference.")

    if a.report:
        with open(a.report, "w") as f:
            json.dump({"a": a.binary_a, "b": a.binary_b, "rows": rows,
                       "only_in_a": only_a, "only_in_b": only_b}, f, indent=2)


if __name__ == "__main__":
    main()
