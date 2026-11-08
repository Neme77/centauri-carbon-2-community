#!/usr/bin/env python3
"""Stufe-0-Verifizierer fuer die K1-Umstrukturierung (ELF32-LE, ARM).

NUR LESEND. Dieses Skript oeffnet Dateien ausschliesslich im Modus 'rb' und
schreibt nie etwas. Es prueft, ob eine bereits umstrukturierte elegoo_printer-
Binary die K1-Transformation korrekt enthaelt (siehe K1_FEASIBILITY.md), und
meldet PASS/FAIL je Pruefpunkt. Es patcht nichts und erzeugt keine Datei.

Aufruf:
    python3 -I stage0_verify.py ORIGINAL_STOCK RESTRUKTURIERT [PAYLOAD_BIN]

- ORIGINAL_STOCK : der unveraenderte Stock-elegoo_printer des neuen Builds.
- RESTRUKTURIERT : die K1-umstrukturierte, aber (fuer diese Stufe) noch
                   UNGEPATCHTE Binary. So prueft Stufe 0 nur die Struktur,
                   getrennt von spaeteren Code-Patches.
- PAYLOAD_BIN    : optional; wenn angegeben, wird geprueft, dass der Payload an
                   der erwarteten Stelle liegt.

Mit nur ORIGINAL_STOCK wird stattdessen der erwartete K1-Plan ausgegeben (Dry).
"""
import struct
import sys

PALIGN = 0x10000


def rd(path):
    with open(path, "rb") as f:   # nur lesend
        return f.read()


def parse(d):
    assert d[:4] == b"\x7fELF" and d[4] == 1 and d[5] == 1, "kein ELF32-LE"
    e_shoff, = struct.unpack_from("<I", d, 0x20)
    e_phoff, = struct.unpack_from("<I", d, 0x1C)
    phentsize, phnum = struct.unpack_from("<HH", d, 0x2A)
    shentsize, shnum, shstrndx = struct.unpack_from("<HHH", d, 0x2E)
    ph = []
    for i in range(phnum):
        b = e_phoff + i * phentsize
        t, off, va, pa, fsz, msz, flg, al = struct.unpack_from("<8I", d, b)
        ph.append(dict(i=i, at=b, type=t, off=off, va=va, fsz=fsz, msz=msz, flg=flg, al=al))
    sh = []
    shstr = struct.unpack_from("<10I", d, e_shoff + shstrndx * shentsize)
    so = shstr[4]
    for i in range(shnum):
        b = e_shoff + i * shentsize
        name, typ, flags, addr, off, size, link, info, align, ent = struct.unpack_from("<10I", d, b)
        nm = d[so + name:d.index(b"\x00", so + name)].decode(errors="replace")
        sh.append(dict(i=i, name=nm, type=typ, addr=addr, off=off, size=size))
    return dict(shoff=e_shoff, e_phoff=e_phoff, phnum=phnum, phentsize=phentsize,
                shentsize=shentsize, shnum=shnum, ph=ph, sh=sh, size=len(d))


def load_exec(ph):
    return next(p for p in ph if p["type"] == 1 and p["flg"] & 1 and p["off"] == 0)


def dry(path):
    d = rd(path); e = parse(d)
    ex = load_exec(e["ph"])
    cut = min(p["off"] for p in e["ph"] if p["type"] == 1 and p["off"] > ex["off"])
    print("== K1-Plan (erwartet), abgeleitet aus Original ==")
    print(f"ORIGINAL Groesse 0x{e['size']:x}")
    print(f"Exec-Segment LOAD[{ex['i']}] endet Datei 0x{ex['off']+ex['fsz']:x} va 0x{ex['va']+ex['fsz']:x}")
    print(f"CUT (naechstes LOAD-Datenstart) = 0x{cut:x}; DELTA = 0x{PALIGN:x}")
    print(f"-> alle p_offset/sh_offset >= 0x{cut:x} : +0x{PALIGN:x}")
    print(f"-> e_shoff 0x{e['shoff']:x} -> 0x{e['shoff']+PALIGN:x}")
    print(f"-> LOAD[{ex['i']}].p_filesz/p_memsz von 0x{ex['fsz']:x} auf Payload-Ende verlaengern")
    print(f"-> Payload-Kandidat VA 0x{ex['va']+ex['fsz']:x} (Datei 0x{ex['off']+ex['fsz']:x}) -- NICHT final")


def verify(orig, new, payload=None):
    od, nd = rd(orig), rd(new)
    oe, ne = parse(od), parse(nd)
    results = []

    def check(name, ok, detail=""):
        results.append((name, ok, detail))

    oex = load_exec(oe["ph"]); nex = load_exec(ne["ph"])
    cut = min(p["off"] for p in oe["ph"] if p["type"] == 1 and p["off"] > oex["off"])
    # DELTA aus einem verschobenen Segment ableiten
    o_l4 = min((p for p in oe["ph"] if p["type"] == 1 and p["off"] > oex["off"]), key=lambda p: p["off"])
    n_l4 = min((p for p in ne["ph"] if p["type"] == 1 and p["off"] > nex["off"]), key=lambda p: p["off"])
    delta = n_l4["off"] - o_l4["off"]
    check("DELTA ein positives Vielfaches von p_align", delta > 0 and delta % PALIGN == 0, f"delta=0x{delta:x}")

    # phdr: Segmente mit off >= cut um delta verschoben, VA unveraendert
    o_by_at = {p["at"]: p for p in oe["ph"]}
    for np in ne["ph"]:
        op = o_by_at.get(np["at"])
        if not op:
            continue
        if op["off"] >= cut and op["type"] in (1, 2, 7):
            check(f"phdr@0x{np['at']:x} p_offset +delta", np["off"] == op["off"] + delta,
                  f"0x{op['off']:x}->0x{np['off']:x}")
            check(f"phdr@0x{np['at']:x} p_vaddr unveraendert", np["va"] == op["va"],
                  f"0x{op['va']:x}->0x{np['va']:x}")
        elif op["type"] == 1 and op["off"] == 0:
            check("LOAD[exec] p_offset/p_vaddr unveraendert", np["off"] == op["off"] and np["va"] == op["va"])
            check("LOAD[exec] p_filesz verlaengert", np["fsz"] >= op["fsz"] + 1, f"0x{op['fsz']:x}->0x{np['fsz']:x}")
            check("LOAD[exec] p_memsz verlaengert", np["msz"] >= op["msz"] + 1, f"0x{op['msz']:x}->0x{np['msz']:x}")
        else:
            check(f"phdr@0x{np['at']:x} unveraendert", np["off"] == op["off"] and np["va"] == op["va"])

    # Kongruenz aller LOAD im neuen Build
    for p in ne["ph"]:
        if p["type"] == 1:
            check(f"Kongruenz LOAD[{p['i']}] p_off≡p_va (mod p_align)",
                  (p["off"] % PALIGN) == (p["va"] % PALIGN))

    # e_shoff
    check("e_shoff +delta", ne["shoff"] == oe["shoff"] + delta, f"0x{oe['shoff']:x}->0x{ne['shoff']:x}")

    # Sektionen nach Name vergleichen
    nmap = {s["name"] + f"#{s['i']}": s for s in ne["sh"]}
    for os_ in oe["sh"]:
        ns = None
        # Match nach Name+Index, sonst nach Name
        cands = [s for s in ne["sh"] if s["name"] == os_["name"]]
        ns = cands[0] if len(cands) == 1 else next((s for s in cands if s["i"] == os_["i"]), None)
        if ns is None:
            check(f".sh {os_['name'] or '(leer)'} gefunden", False, "nicht eindeutig zuordenbar")
            continue
        check(f".sh {os_['name'] or '(idx%d)' % os_['i']} sh_addr unveraendert", ns["addr"] == os_["addr"])
        if os_["off"] >= cut and os_["type"] != 8:  # nicht NOBITS
            check(f".sh {os_['name'] or '(idx%d)' % os_['i']} sh_offset +delta",
                  ns["off"] == os_["off"] + delta, f"0x{os_['off']:x}->0x{ns['off']:x}")
        else:
            check(f".sh {os_['name'] or '(idx%d)' % os_['i']} sh_offset unveraendert",
                  ns["off"] == os_["off"])

    # Byte-Integritaet (Inhalt, unter Ausschluss der Metadaten, die K1 legitim aendert:
    # ELF-Header, phdr-Tabelle, shdr-Tabelle). Erwartet auf der umstrukturierten,
    # noch UNGEPATCHTEN Binary.
    oex_end = oex["off"] + oex["fsz"]
    phdr_end = oe["e_phoff"] + oe["phnum"] * oe["phentsize"]
    # Inhalt des Exec-Segments nach der phdr-Tabelle bis zum alten Exec-Ende (= .text/.rodata/...):
    check("Exec-Inhalt [phdr-Ende, altes Exec-Ende) identisch",
          nd[phdr_end:oex_end] == od[phdr_end:oex_end],
          "Abweichung -> Patches schon drin oder Korruption")
    # Verschobener Inhalt bis zur (alten) shdr-Tabelle, um +delta versetzt:
    o_shoff, n_shoff = oe["shoff"], ne["shoff"]
    check("Verschobener Inhalt [cut, shdr) == old versetzt um +delta",
          nd[cut + delta:n_shoff] == od[cut:o_shoff],
          "verschobener Segmentinhalt nicht byte-identisch (Korruption)")
    # Inhalt hinter der shdr-Tabelle (falls vorhanden) ebenfalls identisch:
    o_tab_end = o_shoff + oe["shnum"] * oe["shentsize"]
    n_tab_end = n_shoff + ne["shnum"] * ne["shentsize"]
    check("Inhalt hinter shdr-Tabelle identisch",
          nd[n_tab_end:] == od[o_tab_end:],
          "Inhalt hinter der Section-Tabelle abweichend")
    # Groesse
    check("Dateigroesse = alt + delta (ohne weitere Anhaenge)",
          ne["size"] == oe["size"] + delta,
          f"0x{oe['size']:x}+0x{delta:x} vs 0x{ne['size']:x}")

    # Payload-Region (optional)
    if payload:
        pb = rd(payload)
        pos = oex_end  # Datei-Offset des Payload-Kandidaten
        check("Payload an erwarteter Stelle",
              nd[pos:pos + len(pb)] == pb, f"@Datei 0x{pos:x}, {len(pb)}B")
        check("Payload innerhalb des neuen Exec-Segments",
              nex["off"] <= pos and pos + len(pb) <= nex["off"] + nex["fsz"])

    # Report
    npass = sum(1 for _, ok, _ in results if ok)
    nfail = len(results) - npass
    print(f"CUT=0x{cut:x}  DELTA=0x{delta:x}")
    for name, ok, detail in results:
        print(f"  [{'PASS' if ok else 'FAIL'}] {name}" + (f"  ({detail})" if detail and not ok else ""))
    print(f"\n{npass} PASS, {nfail} FAIL")
    if nfail:
        print("STUFE 0 NICHT BESTANDEN - nicht flashen.")
    else:
        print("Stufe 0 bestanden (nur Struktur; Emulation + Hardware weiterhin noetig).")
    return 1 if nfail else 0


if __name__ == "__main__":
    args = sys.argv[1:]
    if len(args) == 1:
        dry(args[0]); sys.exit(0)
    if len(args) in (2, 3):
        sys.exit(verify(args[0], args[1], args[2] if len(args) == 3 else None))
    sys.exit("Aufruf: python3 -I stage0_verify.py ORIGINAL_STOCK RESTRUKTURIERT [PAYLOAD_BIN]  (oder nur ORIGINAL_STOCK fuer den Plan)")
