#!/usr/bin/env python3
"""Read-only-Verifizierer fuer eine EXTERN erzeugte, gepatchte + K1-umstrukturierte
elegoo_printer-Binary.

NUR LESEND: oeffnet Dateien mit 'rb', schreibt nichts, patcht nichts. Prueft die
Branch-Ziele der Patch-Stellen gegen die Spezifikation (PAYLOAD_BASE_FIXATION_AND_
BRANCH_PLAN.md): Gruppe A (Payload-Referenzen), Gruppe B (Payload-interne Vendor-
Branches), DIFF-after Einzel-bl, die 9 After-Stellen und die ARM-Reichweite.
Dekodiert vorhandene Branches und vergleicht ihr Ziel mit dem erwarteten; es
erzeugt KEINE imm24-/Patch-Bytes.

Aufruf:
  python3 -I verify_patched.py GEPATCHTE_BINARY [--payload-base 0xcc4fac]
          [--original STOCK_K1] [--report R.json]

Status: Spezifikationswerte sind Arbeitsbasis (nicht final). Ein PASS belegt nur
strukturelle/adressseitige Konsistenz; Emulation + Hardware bleiben noetig.
"""
import argparse
import json
import struct
import sys

PAYLOAD_VA_DEFAULT = 0xCC4FAC
OLD_PAYLOAD_VA = 0xCD77E8
RANGE = 0x2000000  # +-32 MB

# Gruppe B: (alte Quell-VA im Payload, erwartete NEUE Vendor-Ziel-VA, Label)
# Ziel-VAs sind die in README/Branch-Plan verifizierten Werte; #4 strukturell belegt.
GROUP_B = [
    (0xCD781C, 0x8266A0, "bl LanStrategy::start"),
    (0xCD7820, 0x819C28, "b  set_mode+0x68c"),
    (0xCD7828, 0x25859C, "bl get_instance (+0x2530)"),
    (0xCD7840, 0x8267BC, "b  start+0x11c (confirmed, 161/161-instruction disassembly match)"),
    (0xCD787C, 0x8272C8, "bl stop_mqtt_client"),
    (0xCD78C4, 0x8269DC, "bl LanStrategy::report_message"),
    (0xCD78E4, 0x8269DC, "b  LanStrategy::report_message"),
    (0xCD78F4, 0x825148, "b  WanStrategy::report_message"),
    (0xCD78F8, 0x25859C, "bl get_instance (+0x2530)"),
    (0xCD78FC, 0x819C30, "bl get_mode"),
    (0xCD7908, 0x89577C, "b  post_change_message+0x57c"),
    (0xCD7914, 0x0BFFB8, "bl strcmp (PLT, -0xd8)"),
    (0xCD7924, 0x882290, "bl DeviceStatus::getInstance"),
    (0xCD7958, 0x0BF70C, "bl std::string::append (PLT, -0xe4)"),
    (0xCD7964, 0x0BFFB8, "bl strcmp (PLT, -0xd8)"),
    (0xCD79C0, 0x0BF1E4, "b  std::string ctor (PLT, -0xe4)"),
    (0xCD79D8, 0x0BF1E4, "b  std::string ctor (PLT, -0xe4)"),
]

# Payload-Referenzen (Aufrufer-Stellen -> Ziel = PAYLOAD_BASE + Offset):
# (neue Quell-VA der Patch-Stelle, Offset im Payload, Label)
PAYLOAD_REFS = [
    (0x8193A0, 0x5C, "0x811c10 NetworkManager::stop"),
    (0x819400, 0xA4, "0x811c70 report_message"),
    (0x8198B4, 0x5C, "0x812124 set_mode"),
    (0x8267B8, 0x3C, "0x81f028 LanStrategy::start"),
    (0x8954E4, 0xE4, "0x88dd54 post_change_message"),
    (0x895500, 0x110, "0x88dd70 post_change_message"),
    (0x895754, 0x100, "0x88dfc4 post_change_message"),
    (0x8198E8, 0x00, "0x812158 set_mode (DIFF)"),
    (0x858EE4, 0x124, "0x851754 check_token (DIFF)"),
    (0x8590B4, 0x124, "0x851924 check_token (DIFF)"),
    (0x83E8F8, 0x1C4, "0x837168 AsyncMQTTConnector (DIFF)"),
    (0x83E930, 0x1DC, "0x8371a0 AsyncMQTTConnector (DIFF)"),
]

# Cross-Function-After (shift-abhaengig, NICHT Payload): (neue Quell-VA, erwartetes Ziel, Label)
CROSS_FUNC = [
    (0x858644, 0x857160, "0x850eb4 handle_file_cancel -> handle_file_upload"),
    (0x859C90, 0x857140, "0x852500 handle_upload_timeout -> handle_file_upload"),
]

# Verbatim-Stellen (Daten/NOP/In-Funktion, relozierungs-invariant): (Datei-Offset, after-Hex, Label)
# after-Bytes aus http44/network44 (relozierungs-invariant, 1:1 uebernommen).
VERBATIM = [
    (0x816944, "00f020e3", "0x81f1b4 LanStrategy::stop NOP"),
    (0x80939c, "08101be5", "0x811c0c NetworkManager owner (Daten)"),
    (0x8098b0, "a8111be5", "0x812120 NetworkManager owner (Daten)"),
    (0x8463b0, "020053e30b005313", "0x84ec20 admit PRINTING=2 (Daten)"),
    (0x8479c8, "d4fdffeb", "0x850238 upload guard (bl in-func)"),
    (0x848138, "f8fbffeb", "0x8509a8 upload guard (bl in-func)"),
    (0x8852d8, "010053e30130a0930030a083", "0x88db48 formatter (Daten)"),
    (0x809c24, "24ffffea", "0x812494 ensure HTTP started (b in-func)"),
    (0x847790, "5afeffeb", "0x850000 upload guard (bl in-func)"),
    (0x848154, "f9fbffeb", "0x8509c4 completion guard (bl in-func)"),
]


def rd(path):
    with open(path, "rb") as f:   # nur lesend
        return f.read()


def word_at(d, va):
    return struct.unpack_from("<I", d, va - 0x10000)[0]


def branch_target(va, w):
    if (w >> 25) & 7 != 5:
        return None
    imm = w & 0xFFFFFF
    if imm & 0x800000:
        imm -= 1 << 24
    return va + 8 + (imm << 2)


def check_branch(d, src, expect, label, results):
    try:
        w = word_at(d, src)
    except Exception:
        results.append((f"{label} @0x{src:x}", False, "Adresse nicht lesbar"))
        return
    t = branch_target(src, w)
    if t is None:
        results.append((f"{label} @0x{src:x}", False, f"kein Branch (Wort 0x{w:08x})"))
        return
    ok = (t == expect)
    delta = t - src - 8
    rng = (-RANGE <= delta < RANGE) and (delta % 4 == 0)
    results.append((f"{label} @0x{src:x} -> 0x{t:x} (erw 0x{expect:x})", ok and rng,
                    "" if ok and rng else (f"Ziel 0x{t:x} != 0x{expect:x}" if not ok else "ausser Reichweite/unaligned")))


def main():
    ap = argparse.ArgumentParser(description="Read-only-Verifizierer fuer eine gepatchte Binary.")
    ap.add_argument("patched")
    ap.add_argument("--payload-base", default=hex(PAYLOAD_VA_DEFAULT))
    ap.add_argument("--original", help="optional: Stock-K1-Binary fuer Strukturvergleich")
    ap.add_argument("--report")
    a = ap.parse_args()
    base = int(a.payload_base, 16)
    d = rd(a.patched)
    results = []

    # 0. Payload-Bereich belegt?
    try:
        pb = d[base - 0x10000:base - 0x10000 + 500]
        results.append(("Payload-Bereich nicht nur Null (platziert)", any(b != 0 for b in pb),
                        "Payload-Bereich ist Null -> noch nicht platziert"))
    except Exception:
        results.append(("Payload-Bereich lesbar", False, "ausserhalb der Datei"))

    # 1. Gruppe B: neue Quelle = base + (alt - 0xcd77e8)
    for old_src, expect, label in GROUP_B:
        new_src = base + (old_src - OLD_PAYLOAD_VA)
        check_branch(d, new_src, expect, f"[B] {label}", results)

    # 2. Payload-Referenzen (Aufrufer -> base+Offset)
    for src, off, label in PAYLOAD_REFS:
        check_branch(d, src, base + off, f"[A] {label}", results)

    # 3. Cross-Function
    for src, expect, label in CROSS_FUNC:
        check_branch(d, src, expect, f"[X] {label}", results)

    # 4. Verbatim-Stellen (Daten/NOP/In-Funktion): Bytes 1:1 wie im Manifest
    for fo, hexval, label in VERBATIM:
        b = bytes.fromhex(hexval)
        ist = d[fo:fo + len(b)]
        results.append((f"[V] {label} @0x{fo:x}", ist == b,
                        "" if ist == b else f"ist {ist.hex()} != soll {hexval}"))

    # Report
    npass = sum(1 for _, ok, _ in results if ok)
    nfail = len(results) - npass
    print(f"Payload-Basis 0x{base:x}  (Arbeitsbasis, nicht final)")
    for name, ok, detail in results:
        print(f"  [{'PASS' if ok else 'FAIL'}] {name}" + (f"  ({detail})" if detail and not ok else ""))
    print(f"\n{npass} PASS, {nfail} FAIL")
    if nfail:
        print("Nicht konsistent mit der Spezifikation (oder Binary noch nicht vollstaendig gepatcht).")
    else:
        print("Adress-/Branch-Konsistenz OK (Struktur); Emulation + Hardware weiterhin noetig.")
    if a.report:
        with open(a.report, "w") as f:
            json.dump({"payload_base": hex(base),
                       "results": [{"check": n, "pass": ok, "detail": det} for n, ok, det in results],
                       "pass": npass, "fail": nfail,
                       "overall": "PASS" if nfail == 0 else "FAIL"}, f, indent=2)
    return 1 if nfail else 0


if __name__ == "__main__":
    sys.exit(main())
