#!/usr/bin/env python3
"""Deterministische K1-Umstrukturierung der elegoo_printer-Binary (ELF32-LE, ARM).

STRUKTURELL UND VERHALTENSNEUTRAL: verschiebt LOAD[4], DYNAMIC und PT_TLS sowie
die Section-Tabelle um +0x10000, verlaengert das ausfuehrbare Segment LOAD[3] und
reserviert einen 500-Byte-Payload-Bereich an Datei-Offset 0xcb4fac (VA 0xcc4fac).
Es schreibt KEINE Code-, Branch- oder Operanden-Bytes; die Ausgabe ist funktional
identisch zur Stock-Binary. Gruppe-A/B- und DIFF-Branch-Bytes sind NICHT Teil
dieses Schritts. Siehe K1_RESTRUCTURE_SPEC.md.

Aufruf:
  python3 -I k1_restructure.py ORIGINAL OUTPUT [--payload P.bin] [--force] [--report R.json]

Das Original bleibt unveraendert. Die Ausgabe entsteht nur bei vollstaendigem
Stufe-0-PASS (Schreiben in Temp, atomares Umbenennen danach).
"""
import argparse
import hashlib
import json
import os
import struct
import sys
import tempfile

# Eigenes Verzeichnis fuer den lokalen Import ergaenzen (noetig unter `python3 -I`).
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import stage0_verify as s0

PALIGN = 0x10000
EXPECT_CUT = 0xCB5000
DELTA = 0x10000
PAYLOAD_FILE_OFF = 0xCB4FAC
PAYLOAD_VA = 0xCC4FAC
PAYLOAD_SIZE = 500  # 0x1f4


def fail(msg):
    print("FEHLER:", msg, file=sys.stderr)
    sys.exit(2)


def main():
    ap = argparse.ArgumentParser(description="Deterministische K1-Umstrukturierung (strukturell, verhaltensneutral).")
    ap.add_argument("original")
    ap.add_argument("output")
    ap.add_argument("--payload", help="genau 500-Byte Payload; ohne Angabe wird der Bereich Null-reserviert")
    ap.add_argument("--force", action="store_true", help="vorhandene Ausgabe ueberschreiben")
    ap.add_argument("--report", help="JSON-Report-Pfad")
    a = ap.parse_args()

    if os.path.exists(a.output) and not a.force:
        fail(f"Ausgabe existiert: {a.output} (--force noetig)")

    with open(a.original, "rb") as f:   # nur lesend
        od = f.read()
    e = s0.parse(od)
    ex = s0.load_exec(e["ph"])

    # --- Vorbedingungen ---
    if ex["off"] != 0:
        fail("Exec-LOAD nicht bei p_offset 0")
    loads_after = [p for p in e["ph"] if p["type"] == 1 and p["off"] > ex["off"]]
    if not loads_after:
        fail("kein LOAD-Segment hinter dem Exec-Segment")
    cut = min(p["off"] for p in loads_after)
    if cut != EXPECT_CUT:
        fail(f"CUT 0x{cut:x} != erwartet 0x{EXPECT_CUT:x}")
    oex_end = ex["off"] + ex["fsz"]
    if oex_end != PAYLOAD_FILE_OFF:
        fail(f"Exec-Ende 0x{oex_end:x} != PAYLOAD_FILE_OFF 0x{PAYLOAD_FILE_OFF:x}")
    if any(b != 0 for b in od[oex_end:cut]):
        fail("Datei-Luecke [Exec-Ende, CUT) ist nicht Null")
    shift_secs = [s for s in e["sh"] if s["off"] >= cut and s["type"] != 8]  # 8 = NOBITS
    if len(shift_secs) != 10:
        fail(f"{len(shift_secs)} Sektionen mit sh_offset >= CUT (erwartet 10)")
    if DELTA % PALIGN != 0:
        fail("DELTA nicht p_align-konform")
    payload = None
    if a.payload:
        with open(a.payload, "rb") as f:
            payload = f.read()
        if len(payload) != PAYLOAD_SIZE:
            fail(f"Payload {len(payload)} B != {PAYLOAD_SIZE} B")

    # --- Transformation (deterministisch) ---
    nd = bytearray(od[:cut] + b"\x00" * DELTA + od[cut:])
    changes = []

    def patch_u32(off, newval, label):
        old = struct.unpack_from("<I", nd, off)[0]
        struct.pack_into("<I", nd, off, newval)
        changes.append({"field": label, "at": hex(off), "old": hex(old), "new": hex(newval)})

    patch_u32(0x20, e["shoff"] + DELTA, "e_shoff")                       # 2. e_shoff
    for p in e["ph"]:                                                    # 3. phdr p_offset
        if p["off"] >= cut and p["type"] in (1, 2, 7):
            patch_u32(p["at"] + 4, p["off"] + DELTA, f"phdr[{p['i']}].p_offset")
    newsz = PAYLOAD_FILE_OFF + PAYLOAD_SIZE                              # 4. LOAD[3] groesse
    patch_u32(ex["at"] + 16, newsz, "LOAD_exec.p_filesz")
    patch_u32(ex["at"] + 20, newsz, "LOAD_exec.p_memsz")
    new_shoff = e["shoff"] + DELTA                                       # 5. shdr sh_offset
    shent = e["shentsize"]
    for s in shift_secs:
        patch_u32(new_shoff + s["i"] * shent + 0x10, s["off"] + DELTA,
                  f"sh[{s['i']}:{s['name'] or 'idx'}].sh_offset")

    reserved = payload is None                                          # 6. Payload/Reserve
    nd[PAYLOAD_FILE_OFF:PAYLOAD_FILE_OFF + PAYLOAD_SIZE] = (
        bytes(PAYLOAD_SIZE) if reserved else payload
    )

    # --- Nachbedingungen: Schreiben in Temp, Stufe-0-Verifikation, dann erst gueltig ---
    out_dir = os.path.dirname(os.path.abspath(a.output)) or "."
    fd, tmp = tempfile.mkstemp(dir=out_dir, prefix=".k1tmp_")
    try:
        with os.fdopen(fd, "wb") as f:
            f.write(nd)
        print("--- Stufe-0-Nachpruefung ---")
        rc = s0.verify(a.original, tmp, a.payload)
        if rc != 0:
            os.remove(tmp)
            fail("Stufe-0-Nachpruefung FAIL -> keine Ausgabe geschrieben")
        if os.path.exists(a.output):
            if not a.force:
                os.remove(tmp)
                fail(f"Ausgabe existiert: {a.output} (--force noetig)")
            os.remove(a.output)
        os.rename(tmp, a.output)
    except SystemExit:
        raise
    except Exception as exc:
        if os.path.exists(tmp):
            os.remove(tmp)
        fail(f"Ausnahme waehrend Schreiben/Pruefen: {exc!r}")

    report = {
        "original": a.original,
        "output": a.output,
        "input_sha256": hashlib.sha256(od).hexdigest(),
        "output_sha256": hashlib.sha256(bytes(nd)).hexdigest(),
        "cut": hex(cut),
        "delta": hex(DELTA),
        "payload": "reserved_zero" if reserved else a.payload,
        "payload_file_off": hex(PAYLOAD_FILE_OFF),
        "payload_va": hex(PAYLOAD_VA),
        "payload_size": PAYLOAD_SIZE,
        "changes": changes,
        "overall": "PASS",
    }
    if a.report:
        with open(a.report, "w") as f:
            json.dump(report, f, indent=2)
    print(f"\nK1-Umstrukturierung OK -> {a.output}")
    print(f"  input  sha256 {report['input_sha256']}")
    print(f"  output sha256 {report['output_sha256']}")
    print(f"  Payload-Bereich: {'reserviert (Null)' if reserved else a.payload}")
    print(f"  geaenderte Felder: {len(changes)}")


if __name__ == "__main__":
    main()
