# Ausführungs-Runbook: Druckerport 02.01.01.13

Schritt-für-Schritt für die **eigene** Durchführung. Claude bereitet vor und
prüft; **du** berechnest und schreibst die Branch-Bytes, emulierst und testest.
Dieses Dokument enthält **keine fertigen Patch-Bytes und keine imm24-Werte** —
nur Formeln, Zwischenschritte und die belegten Adressen. Prüfgröße ist stets die
**Zieladresse** (decodiert von `verify_patched.py`), kein Byte-Literal.

Trennlinie:
- **Claude (erledigt):** Analyse, Adressen/Shifts, K1-Umstrukturierung (verhaltensneutral),
  `stage0_verify.py`, `verify_patched.py`, Formel (`PORTING_METHODOLOGY.md`), Pläne.
- **Du (byte-schreibend):** Payload-Code + Branch-Bytes erzeugen, anwenden, emulieren, testen.

> **Wichtige Abhängigkeit:** Der 500-Byte-Payload enthält die Gruppe-B-Branches.
> Deshalb berechnest du die Gruppe-B-Bytes **vor** dem Einsetzen und baust daraus
> die Payload-Datei. Reihenfolge daher: **2a (Gruppe B) → 1 (Payload einsetzen) →
> 2b (externe Stellen) → 3 (anwenden) → 4 (verifizieren) → 5 (Emulation) → 6 (HW).**

---

## 2a. Branch-Bytes berechnen — Gruppe B (payload-intern), zuerst

Formel (ARM A32, aus `PORTING_METHODOLOGY.md`), je Branch:
```
delta = Ziel_neu − Quelle_neu − 8
prüfe: delta mod 4 == 0  und  −0x2000000 ≤ delta < 0x2000000
imm24 = (delta >> 2) & 0xFFFFFF
Wort  = 0xEA000000 | imm24   (b)   bzw.   0xEB000000 | imm24   (bl)
        bei bedingten Branches die Bedingung im oberen Nibble statt 0xE
Bytes = Wort little-endian
```
Quelle_neu im Payload = `PAYLOAD_BASE_NEW + (Quelle_alt − 0xcd77e8)` mit
`PAYLOAD_BASE_NEW = 0xcc4fac`. Ziele siehe Tabelle (alle in `README.md` /
`PAYLOAD_BASE_FIXATION_AND_BRANCH_PLAN.md` belegt):

| # | Quelle_alt | Quelle_neu | Ziel_neu | Shift | Typ |
|---|---|---|---|---|---|
| 1 | `0xcd781c` | `0xcc4fe0` | `0x8266a0` | `+0x7790` | `bl` |
| 2 | `0xcd7820` | `0xcc4fe4` | `0x819c28` | `+0x7790` | `b` |
| 3 | `0xcd7828` | `0xcc4fec` | `0x25859c` | `+0x2530` | `bl` |
| 4 | `0xcd7840` | `0xcc5004` | `0x8267bc` | `+0x7790` | `b` (eingegrenzt) |
| 5 | `0xcd787c` | `0xcc5040` | `0x8272c8` | `+0x7790` | `bl` |
| 6 | `0xcd78c4` | `0xcc5088` | `0x8269dc` | `+0x7790` | `bl` |
| 7 | `0xcd78e4` | `0xcc50a8` | `0x8269dc` | `+0x7790` | `b` |
| 8 | `0xcd78f4` | `0xcc50b8` | `0x825148` | `+0x7790` | `b` |
| 9 | `0xcd78f8` | `0xcc50bc` | `0x25859c` | `+0x2530` | `bl` |
| 10 | `0xcd78fc` | `0xcc50c0` | `0x819c30` | `+0x7790` | `bl` |
| 11 | `0xcd7908` | `0xcc50cc` | `0x89577c` | `+0x7790` | `b` |
| 12 | `0xcd7914` | `0xcc50d8` | `0xbffb8` | `-0xd8` | `bl` (PLT) |
| 13 | `0xcd7924` | `0xcc50e8` | `0x882290` | `+0x7790` | `bl` |
| 14 | `0xcd7958` | `0xcc511c` | `0xbf70c` | `-0xe4` | `bl` (PLT) |
| 15 | `0xcd7964` | `0xcc5128` | `0xbffb8` | `-0xd8` | `bl` (PLT) |
| 16 | `0xcd79c0` | `0xcc5184` | `0xbf1e4` | `-0xe4` | `b` (PLT) |
| 17 | `0xcd79d8` | `0xcc519c` | `0xbf1e4` | `-0xe4` | `b` (PLT) |

Verifikationsmethode: für jeden berechneten Branch rückwärts prüfen
`Ziel == Quelle_neu + 8 + (sign_extend(imm24) << 2)` **und** dass die Zielinstruktion
im neuen Build die erwartete Funktion/Stelle trägt. PLT-Ziele sind direkte Branches
auf den Stub (Sprung über GOT). Die payload-**internen** Branches (payload→payload)
behalten ihre relative Distanz, da der ganze Block starr verschoben wird.

Daraus baust du die **500-Byte-Payload-Datei**: alte Payload-Bytes übernehmen, nur
die 17 Branch-Worte (an ihren Offsets) durch die neu berechneten ersetzen. Die
Datei muss exakt 500 Byte groß sein.

---

## 1. Payload einsetzen (`k1_restructure.py`)

```bash
cd experimental/firmware-port-0113
cp ../../builder/current/analysis/02.01.01.13/rootfs/opt/bin/elegoo_printer ./WORK/elegoo_printer.stock
python3 -I k1_restructure.py ./WORK/elegoo_printer.stock ./WORK/elegoo_printer.k1 \
    --payload ./WORK/payload_500.bin --report ./WORK/k1_report.json
```
- **Eingabe:** Stock-`elegoo_printer` (neu, `f0111e0b…`). **Output:** `elegoo_printer.k1`
  (nie das Original überschreiben; arbeite in `WORK/`, außerhalb des Repos).
- **Payload:** `payload_500.bin`, genau 500 Byte, aus 2a.
- **Prüfungen davor:** Vorbedingungen im Skript (CUT, Lücke-null, 10 Sektionen, Payload-Größe).
- **Prüfungen danach:** integrierte Stufe-0-Verifikation (82 Checks) — die Ausgabe
  entsteht nur bei vollständigem PASS; sonst keine (partielle) Datei.
- **Report (`k1_report.json`):** `overall` muss `PASS` sein; `changes` listet die 16
  Strukturfelder; `payload` zeigt die Payload-Quelle statt `reserved_zero`.
- **Bei FAIL:** Ausgabe wird nicht geschrieben/gelöscht. Fehlermeldung lesen, Ursache
  beheben (meist Payload-Größe oder falsche Eingabedatei), erneut.

---

## 2b. Branch-Bytes berechnen — externe Stellen (gleiche Formel)

Alle Quelle_neu sind reale Adressen im umstrukturierten Build (unverändert ggü. Stock,
da `.text` nicht verschoben wird).

**Payload-Referenzen (Ziel = `0xcc4fac + Offset`):**

| Patch-Stelle (neu) | Offset | Ziel_neu | Typ |
|---|---|---|---|
| `0x8193a0` | `+0x5c` | `0xcc5008` | `bl`/`b` |
| `0x819400` | `+0xa4` | `0xcc5050` | |
| `0x8198b4` | `+0x5c` | `0xcc5008` | |
| `0x8267b8` | `+0x3c` | `0xcc4fe8` | |
| `0x8954e4` | `+0xe4` | `0xcc5090` | |
| `0x895500` | `+0x110` | `0xcc50bc` | |
| `0x895754` | `+0x100` | `0xcc50ac` | |
| `0x8198e8` (DIFF) | `+0x00` | `0xcc4fac` | |
| `0x858ee4` (DIFF) | `+0x124` | `0xcc50d0` | |
| `0x8590b4` (DIFF) | `+0x124` | `0xcc50d0` | |
| `0x83e8f8` (DIFF) | `+0x1c4` | `0xcc5170` | |
| `0x83e930` (DIFF) | `+0x1dc` | `0xcc5188` | |

**Cross-Function (shift-abhängig, nicht Payload):**

| Patch-Stelle (neu) | Ziel_neu | Shift |
|---|---|---|
| `0x858644` | `0x857160` | `+0x7790` |
| `0x859c90` | `0x857140` | `+0x7790` |

**DIFF-Block-Einbauten (`0x812128`→`0x8198b8`, `0x84f940`→`0x8570d0`):** deren
after-interne Branches behalten bei `+0x7790` ihre Distanz; die externen Ziele
(`start_http_server_file`, `handle_file_cancel`, `remove_client`, `getInstance`,
`set_status`×2, `set_sub_status`) sind in `DIFF_BLOCKS.md` belegt. Instruktionstyp
je Operand aus dem Listing übernehmen.

Für jede Stelle gilt dieselbe Formel und dieselbe Rückwärts-Verifikation wie in 2a.
Instruktionstyp (`b`/`bl`/bedingt) **aus dem Listing der jeweiligen Stelle** übernehmen,
nicht raten.

---

## 3. Patch anwenden

- **Datei:** immer eine **Kopie** (`elegoo_printer.k1` in `WORK/`), **nie** das Original.
- **Offsets:** Datei-Offset = `VA − 0x10000` (für `.text`; gilt in diesem Build durchgängig).
  Also Patch-Stelle `0x8193a0` → Datei `0x8093a0` usw.
- **Erwartete Bytes:** die von **dir** aus der Formel berechneten 4 Byte je Stelle
  (Claude gibt sie nicht aus). Vor dem Schreiben die **vorhandenen** 4 Byte an der
  Stelle lesen und mit dem erwarteten **before** vergleichen (aus Manifest/Listing),
  um die richtige Stelle zu bestätigen.
- **Prüfen nach dem Schreiben:** erneut 4 Byte lesen und mit dem berechneten Wort vergleichen.
- **Bei Abweichung:** Stelle nicht weiterschreiben; Kopie verwerfen, neu aus der
  unveränderten `elegoo_printer.k1` beginnen. Nie am Original patchen.

---

## 4. Verifizieren (`verify_patched.py`, read-only)

```bash
python3 -I verify_patched.py ./WORK/elegoo_printer.k1 --report ./WORK/verify.json
```
- **32 Checks:** 1 Payload-Bereich-belegt + 17 Gruppe B + 12 Payload-Referenzen + 2 Cross-Function.
- Jeder Check dekodiert den Branch an der Stelle und vergleicht **Ziel == erwartet**
  plus ARM-Reichweite. Keine Byte-Literale nötig.
- **Erwartung:** nach vollständigem Patch **32 PASS, 0 FAIL**, Exit 0, Report
  `overall: PASS`.
- **Bei FAIL:** die genannte Stelle hat ein falsches Ziel (falsches imm24, falsche
  Quelle/Offset oder Payload nicht/falsch platziert). Korrigieren, Schritt 3→4 wiederholen.
- Auf der **noch ungepatchten** Datei meldet er erwartungsgemäß alle 32 als FAIL.

---

## 5. Emulation (`EMULATION_PLAN.md`)

- **Jetzt (Phase α, vor/ohne Payload nötig):** E1 GUI, E2 Kamera, E6 LanStrategy-NOP,
  E11 Dual, E3/E4/E5 HTTP-Guards + `device_status_offset`, E10-Teil (2 Block-Einbauten).
- **Nach Payload-Basis (Phase β):** E7 Payload-Koexistenz, E8 Gruppe B, E9 PLT/GOT.
- **Nach DIFF-after der 4 Einzel-`bl` (Phase γ):** E10-Rest.
- Je Test: Eingabe, erwartetes/tatsächliches Verhalten, Logs, Pass/Fail, Abbruch —
  Details in `EMULATION_PLAN.md`.
- **Ausdrücklich:** „Gerät startet" bzw. „Emulation läuft durch" ist **kein**
  Erfolgskriterium; Erfolg ist nur der korrekte Kontrollfluss je Status.
- **Gate:** kein Hardware-Test, bevor die zugehörigen E-Fälle PASS sind.

---

## 6. Hardware-Test (`HARDWARE_TEST_*.md`, `ROLLBACK_PLAN.md`)

**Mindestvoraussetzungen:** Original-Hashes notiert, Backups erstellt und per
Prüfsumme verifiziert, reproduzierbarer Restore-Pfad geprüft, K1-Entscheidung
getroffen, zugehörige Emulation bestanden.

**Testreihenfolge:** HW-00 Referenz → HW-01 Boot/Brick → HW-02 GUI → HW-03 Kamera →
HW-04 Grundfunktionen → HW-15 lageunabhängige Stellen → HW-21 Rollback → **erst
danach** Payload/DIFF (HW-05/06/08/10–14/16/17).

Je Test: Voraussetzung, erwartetes Verhalten, Pass/Fail, Abbruchkriterium,
Rollback-Pfad — Details in `HARDWARE_TEST_CHECKLIST.md`. Nachweise in
`TEST_EVIDENCE_TEMPLATE.md`.

**Ausdrücklich:** Kein Hardware-Test ohne geprüften, reproduzierbaren
Wiederherstellungsweg. Software-Rollback ersetzt keinen physischen Flash-Readback.

---

## Abschluss / Freigabe

`ported_patch_set=true` erst, wenn: alle `verify_patched.py`-Checks PASS **und**
alle relevanten E-Fälle PASS **und** die Hardware-Checkliste am Gerät grün ist.
Vorher kein Commit, keine Veröffentlichung, kein Release.
