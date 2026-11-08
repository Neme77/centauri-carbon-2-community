# Payload-Basis & Branch-Plan (Spezifikation)

Arbeitsdokument, nur lokal. **Reine Spezifikation**: keine Branch-Bytes, keine
`imm24`-Werte, keine Operanden, keine finalen Patch-Bytes. Zieladressen werden
berechnet (Adressarithmetik), die Instruktions­kodierung bleibt der spätere,
separate byte-schreibende Schritt. Grundlagen: `K1_FEASIBILITY.md`,
`K1_RESTRUCTURE_SPEC.md`, `README.md` („Payload placement“), `DIFF_BLOCKS.md`.

## Ausgangslage

- K1-Umstrukturierung abgeschlossen und verifiziert (`k1_restructure.py`).
- Stufe 0: 82 PASS / 0 FAIL; Dateigröße exakt `+0x10000`; 16 erwartete Felder;
  VAs/Kongruenz intakt; deterministisch.
- Payload-Bereich **reserviert (Null)**, noch nicht mit echtem Payload gefüllt.
- Die umstrukturierte Binary ist funktional identisch zur Stock-Binary.

## Payload-Basis

| Feld | Wert |
|---|---|
| `PAYLOAD_BASE_NEW` (VA) | `0xcc4fac` |
| Datei-Offset | `0xcb4fac` |
| Größe | 500 B (`0x1f4`), Ende VA `0xcc51a0` |
| Ausrichtung | 4-Byte |
| Relokationsdelta ggü. alt (`0xcd77e8`) | `-0x1283c` (Quelladressen im Payload sinken um diesen Betrag) |
| Status | **Arbeitsbasis, NICHT final** — final erst nach Emulation + Hardware-Test |

## Gruppe A — Referenzen in den Payload

Neue Ziel-VA = `PAYLOAD_BASE_NEW + Offset_im_Payload` (= `0xcc4fac + Offset`).

| Offset im Payload | Alte Ziel-VA | **Neue Ziel-VA** | Aufrufer | Formel | Status |
|---|---|---|---|---|---|
| `+0x00` | `0xcd77e8` | `0xcc4fac` | `0x812158` (DIFF) | base+0x00 | berechnet |
| `+0x3c` | `0xcd7824` | `0xcc4fe8` | `0x81f028` | base+0x3c | berechnet |
| `+0x5c` | `0xcd7844` | `0xcc5008` | `0x812124`, `0x811c10` | base+0x5c | berechnet |
| `+0xa4` | `0xcd788c` | `0xcc5050` | `0x811c70` | base+0xa4 | berechnet |
| `+0xe4` | `0xcd78cc` | `0xcc5090` | `0x88dd54` | base+0xe4 | berechnet |
| `+0x100` | `0xcd78e8` | `0xcc50ac` | `0x88dfc4` | base+0x100 | berechnet |
| `+0x110` | `0xcd78f8` | `0xcc50bc` | `0x88dd70` | base+0x110 | berechnet |
| `+0x124` | `0xcd790c` | `0xcc50d0` | `0x851754`, `0x851924` (DIFF) | base+0x124 | berechnet |
| `+0x1c4` | `0xcd79ac` | `0xcc5170` | `0x837168` (DIFF) | base+0x1c4 | berechnet |
| `+0x1dc` | `0xcd79c4` | `0xcc5188` | `0x8371a0` (DIFF) | base+0x1dc | berechnet |

DIFF-Kontext: `0x812158`, `0x851754`, `0x851924`, `0x837168`, `0x8371a0` gehören
zu den rekompilierten Blöcken (siehe DIFF-Abschnitt). Alle neuen Ziele liegen in
`[0xcc4fac, 0xcc51a0)` (Payload). Keine Branch-Bytes, keine `imm24`.

## Gruppe B — Branches im Payload auf Vendor-Funktionen

Neue Quell-VA im Payload = `PAYLOAD_BASE_NEW + (alte Quell-VA − 0xcd77e8)`
(= alt `− 0x1283c`). Vendor-Ziel-VAs sind bereits verifiziert (`README.md`).

| Branch | Quell-VA alt | Vendor-Ziel | Ziel alt | **Ziel neu** | Shift | Shift? | Referenzform | Grundlage | Status |
|---|---|---|---|---|---|---|---|---|---|
| 1 | `0xcd781c` | `LanStrategy::start()` | `0x81ef10` | `0x8266a0` | `+0x7790` | Ja | `bl` | Einsprung Off 0 | verifiziert |
| 2 | `0xcd7820` | `NetworkManager::set_mode` +0x68c | `0x812498` | `0x819c28` | `+0x7790` | Ja | `b` | Fenster identisch | verifiziert |
| 3 | `0xcd7828` | `NetworkManager::get_instance()` | `0x25606c` | `0x25859c` | `+0x2530` | Ja | `bl` | Einsprung Off 0 | verifiziert |
| 4 | `0xcd7840` | `LanStrategy::start()` +0x11c | `0x81f02c` | `0x8267bc` | `+0x7790` | **bestätigt** | `b` | Volle Capstone-Disassemblierung: 161/161 Instruktionen der Funktion identisch, Patch-Stelle Index 71 in beiden Builds `mov r0,r3` | bestätigt (2026-11-08) |
| 5 | `0xcd787c` | `LanStrategy::stop_mqtt_client()` | `0x81fb38` | `0x8272c8` | `+0x7790` | Ja | `bl` | Einsprung Off 0 | verifiziert |
| 6 | `0xcd78c4` | `LanStrategy::report_message()` | `0x81f24c` | `0x8269dc` | `+0x7790` | Ja | `bl` | Einsprung Off 0 | verifiziert |
| 7 | `0xcd78e4` | `LanStrategy::report_message()` | `0x81f24c` | `0x8269dc` | `+0x7790` | Ja | `b` | Einsprung Off 0 | verifiziert |
| 8 | `0xcd78f4` | `WanStrategy::report_message()` | `0x81d9b8` | `0x825148` | `+0x7790` | Ja | `b` | Einsprung Off 0 | verifiziert |
| 9 | `0xcd78f8` | `NetworkManager::get_instance()` | `0x25606c` | `0x25859c` | `+0x2530` | Ja | `bl` | Einsprung Off 0 | verifiziert |
| 10 | `0xcd78fc` | `NetworkManager::get_mode()` | `0x8124a0` | `0x819c30` | `+0x7790` | Ja | `bl` | Einsprung Off 0 | verifiziert |
| 11 | `0xcd7908` | `DeviceStatus::post_change_message` +0x57c | `0x88dfec` | `0x89577c` | `+0x7790` | Ja | `b` | Fenster identisch | verifiziert |
| 12 | `0xcd7914` | `strcmp` | `0xc0090` | `0xbffb8` | `-0xd8` | Ja | direkter Branch auf PLT-Stub, Sprung über GOT | `.rel.plt` | verifiziert |
| 13 | `0xcd7924` | `DeviceStatus::getInstance()` | `0x87ab00` | `0x882290` | `+0x7790` | Ja | `bl` | Einsprung Off 0 | verifiziert |
| 14 | `0xcd7958` | `std::string::append` | `0xbf7f0` | `0xbf70c` | `-0xe4` | Ja | direkter Branch auf PLT-Stub, Sprung über GOT | `.rel.plt` | verifiziert |
| 15 | `0xcd7964` | `strcmp` | `0xc0090` | `0xbffb8` | `-0xd8` | Ja | direkter Branch auf PLT-Stub, Sprung über GOT | `.rel.plt` | verifiziert |
| 16 | `0xcd79c0` | `std::string` ctor | `0xbf2c8` | `0xbf1e4` | `-0xe4` | Ja | direkter Branch auf PLT-Stub, Sprung über GOT | `.rel.plt` | verifiziert |
| 17 | `0xcd79d8` | `std::string` ctor | `0xbf2c8` | `0xbf1e4` | `-0xe4` | Ja | direkter Branch auf PLT-Stub, Sprung über GOT | `.rel.plt` | verifiziert |

**13 von 13 Zielen verifiziert** (inkl. `LanStrategy::start+0x11c` → `0x8267bc`,
bestätigt durch vollständige Disassemblierung am 2026-11-08).
**Kein Pauschal-Shift** — vier Shifts: `+0x7790` (9 Ziele), `+0x2530` (1), `-0xd8`
(strcmp-PLT), `-0xe4` (zwei std::string-PLT). Alle direkte Branches; PLT = direkter
Branch auf Stub, Sprung über GOT. Keine Branch-Bytes, keine `imm24`.

## ARM-Reichweite

- Bedingte Payload-Basis: `0xcc4fac`.
- Entferntestes Ziel: `0x25859c` (`get_instance`), Entfernung ~10,9 MB.
- Alle Ziele innerhalb ±32 MB.
- **Vorläufig**, bis die finale Payload-Basis feststeht; nach finaler Platzierung
  je Referenz mit tatsächlicher Quell-/Zieladresse erneut zu bestätigen.

## DIFF-after-Operanden

| Alte Adr. | Neue Adr. | Kontext | Before-Status | After-Status | Betroffene Operanden | Payload-Abhängig? |
|---|---|---|---|---|---|---|
| `0x812128` | `0x8198b8` | `set_mode`-Block | belegt (nur Relozierung) | offen | after-interne Branches + `bl start_http_server_file` | nein |
| `0x84f940` | `0x8570d0` | `handle_file_upload`-Block | belegt (nur Relozierung) | offen | after-interne Branches + `handle_file_cancel`/`remove_client`/`getInstance`/`set_status`×2/`set_sub_status` | nein |
| `0x837168` | `0x83e8f8` | `AsyncMQTTConnector` | belegt | offen | einzelner `bl` → `payload+0x1c4` | **ja** |
| `0x8371a0` | `0x83e930` | `AsyncMQTTConnector` | belegt | offen | einzelner `bl` → `payload+0x1dc` | **ja** |
| `0x851754` | `0x858ee4` | `check_token` | belegt | offen | einzelner `bl` → `payload+0x124` | **ja** |
| `0x851924` | `0x8590b4` | `check_token` | belegt | offen | einzelner `bl` → `payload+0x124` | **ja** |

5/6 before-Fenster als reine Relozierung belegt; `0x812158` (zum `set_mode`-Umbau
gehörig) ist HTTP-Patch-abhängig, ohne Stock-zu-Stock-Vergleich. Keine Operanden-Werte.

## Die 9 After-Stellen

| Stelle | Typ | Abhängigkeit | Benötigte Berechnung | Status |
|---|---|---|---|---|
| `0x811c10` | Payload-Referenz | Payload-Basis | neues Ziel = `payload+0x5c` (`0xcc5008`) | offen |
| `0x811c70` | Payload-Referenz | Payload-Basis | `payload+0xa4` (`0xcc5050`) | offen |
| `0x812124` | Payload-Referenz | Payload-Basis | `payload+0x5c` (`0xcc5008`) | offen |
| `0x81f028` | Payload-Referenz | Payload-Basis | `payload+0x3c` (`0xcc4fe8`) | offen |
| `0x88dd54` | Payload-Referenz | Payload-Basis | `payload+0xe4` (`0xcc5090`) | offen |
| `0x88dd70` | Payload-Referenz | Payload-Basis | `payload+0x110` (`0xcc50bc`) | offen |
| `0x88dfc4` | Payload-Referenz | Payload-Basis | `payload+0x100` (`0xcc50ac`) | offen |
| `0x850eb4` | Cross-Function-Branch | `+0x7790`-Shift (NICHT Payload) | neues Ziel `0x84f9d0+0x7790 = 0x857160` | offen |
| `0x852500` | Cross-Function-Branch | `+0x7790`-Shift (NICHT Payload) | neues Ziel `0x84f9b0+0x7790 = 0x857140` | offen |

7 Payload-Referenzen (hängen an `PAYLOAD_BASE_NEW`), 2 Cross-Function-Branches
(hängen am `+0x7790`-Funktions-Shift, nicht an der Payload-Basis). Keine Branch-Bytes.

## Verifikations- und Testreihenfolge

| # | Stufe | Voraussetzung | Erwartetes Ergebnis | Pass/Fail | Rollback-Pfad | Abbruch |
|---|---|---|---|---|---|---|
| 1 | Payload-Basis fixieren | Stufe 0 bestanden | `PAYLOAD_BASE_NEW=0xcc4fac` dokumentiert, Payload platziert | Stufe 0 erneut PASS mit `--payload` | Original behalten | Stufe-0-FAIL |
| 2 | Gruppe A berechnen | Basis fix | 10 neue Ziel-VAs = base+Offset | je Stelle Zieladresse in `[base,base+0x1f4)` | Ausgabe verwerfen | Ziel außerhalb Payload |
| 3 | Gruppe B berechnen | Basis fix, Vendor-Ziele verifiziert | 12 Ziele belegt, 1 UNKLAR disassembliert | Instruktion am neuen Ziel = erwartete Funktion | Ausgabe verwerfen | UNKLAR ungelöst |
| 4 | DIFF-after berechnen | Gruppe A/B fix | 6 Blöcke after-Operanden hergeleitet | before-Fenster passt, Ziele belegt | Ausgabe verwerfen | Operand unbelegt |
| 5 | 9 After-Stellen berechnen | Basis + Shift fix | 7 Payload-, 2 Shift-Ziele | Zieladressen belegt | Ausgabe verwerfen | Ziel unbelegt |
| 6 | Emulation | 1–5 fertig, Bytes erzeugt | E-Fälle grün (`TEST_MATRIX.md`) | alle relevanten E-Fälle PASS | Ausgabe verwerfen | E-Fall FAIL |
| 7 | Hardware-Test | Emulation grün, Rollback geprüft | H-Fälle grün (`HARDWARE_TEST_*`) | H-Fälle PASS | Referenz-FW (`ROLLBACK_PLAN.md`) | jedes Abbruchsymptom |

„Erst rechnen, dann patchen, dann gegen die erwartete Instruktion verifizieren“ —
keine Blind-Patches. „Gerät startet“ ist **kein** Erfolgskriterium.

## Rollback-Gates

- **Vor jedem Hardware-Test:** Original-Hashes, Backup-Pfade, exakter
  Wiederherstellungsweg dokumentiert und geprüft.
- **Vor Payload-Platzierung:** Rollback-Plan für K1 bewertet; die umstrukturierte
  Binary ist eine neue Datei, Original bleibt unberührt.
- **Vor DIFF-After-Tests:** Rollback für alle betroffenen Stellen nachvollziehbar.
- **Kein Hardware-Test ohne geprüften, reproduzierbaren Wiederherstellungsweg.**
- **Software-Rollback ersetzt keinen physischen Flash-Readback.**

## Abschluss

- **Payload-Basis belastbar?** Als Arbeitsbasis ja (Stufe 0 belegt, deterministisch,
  verhaltensneutral), final erst nach Emulation + Hardware-Test.
- **Gruppe A:** 10 Referenzen, neue Ziel-VAs berechnet (`base+Offset`, alle im Payload).
- **Gruppe B:** 17 Branches / 13 Ziele; 12 verifiziert, 1 UNKLAR; vier Shifts.
- **DIFF-after:** 6 Blöcke; 2 Block-Einbauten (ohne Payload), 4 Einzel-`bl` (Payload-abhängig).
- **9 After-Stellen:** 7 Payload-abhängig, 2 Shift-abhängig.
- **Reihenfolge:** Basis → A → B → DIFF-after → 9 After → Emulation → Hardware.
- **Rollback-Gates:** Hashes/Backups/Restore vor jedem HW-Test; K1-Rollback bewertet;
  DIFF-After-Rollback nachvollziehbar; kein Test ohne reproduzierbaren Rückweg;
  Software-Rollback ≠ physischer Readback.

**Ausdrückliche Grenzen:** keine Branch-Bytes, keine `imm24`, keine Operanden, keine
finalen Patch-Bytes, keine Emulation, kein Hardware-Test, kein Commit, keine
Veröffentlichung in diesem Dokument.
