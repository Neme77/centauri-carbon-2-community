# Risikoliste: Port 02.01.01.07 → 02.01.01.13

Arbeitsdokument, nur lokal. Führt alle Stellen, die **ohne Emulation oder
Hardware-Test nicht als sicher gelten** können. Keine Patch-Bytes, keine Adresse
final. Freigabe-Spalte verweist auf die Testfälle in `TEST_MATRIX.md`.

**Grundsatz:** Bis ein vollständig portierter, emulations- UND hardware-geprüfter
Satz vorliegt, gilt **jede** Stelle als nicht freigegeben. `ported_patch_set`
bleibt `false`. Die Stufen unten ordnen nur das Rest-Risiko relativ zueinander.

Stufen: **HOCH** (blockiert/unaufgelöst, Brick- oder Fehlfunktionsrisiko) ·
**MITTEL** (belegt, aber abhängig oder mit individuellem Shift) ·
**NIEDRIG** (strukturell belegt oder byte-identisch, nur Bestätigung offen).

## Risikotabelle

| ID | Bereich / Stelle | Risiko-Art | Stufe | Warum nicht sicher | Freigabe durch |
|---|---|---|---|---|---|
| R1 | Payload-Platzierung `0xcd77e8` | strukturelle ELF-Umstrukturierung | **HOCH** | nur 84 Byte frei; Segmentverschiebung nötig; kein Byte-Patch; Fehler = Brick/Segmentkorruption | Toolchain-Platzierung + E7 + H5/H7 |
| R2 | Payload-Referenzen Gruppe A (12 Aufruferstellen → Block) | Payload-abhängig | **HOCH** | Zieladresse = Payload-Basis + Offset; falsche Basis → Sprung ins Leere | finale Basis + E7/E8 |
| R3 | `LanStrategy::start+0x11c` (`0xcd7840`, Gruppe B) | unaufgelöstes Ziel | **HOCH** | Zielfenster verschoben, neue VA nicht belegt | Disassemblierung + E8 |
| R4 | 6 DIFF-Blöcke (`set_mode`, `handle_file_upload`-Block, `AsyncMQTTConnector` ×2, `check_token` ×2) | rekompiliert (F) | **HOCH** | neue Funktionslogik, Patches nicht hergeleitet | Disassemblierung + E10 |
| R5 | Cross-Function-Branches `0x850eb4`, `0x852500` | X | **MITTEL** | Ziel in anderer Funktion; after muss neu gezielt werden | E3 |
| R6 | Individueller Shift: `get_instance` `+0x2530` | abweichender Shift | **MITTEL** | pauschal `+0x7790` wäre falsch; Ziel belegt, aber Sonderfall | E8 |
| R7 | PLT/GOT: `strcmp` `-0xd8`, `std::string` ×2 `-0xe4` | G, individueller Shift | **MITTEL** | neu gebauter PLT; Stub-Adresse pro Symbol verschieden | E9 |
| R8 | Gruppe-B-Ziele mit `+0x7790` (9) | Payload-abhängige Bytes | **MITTEL** | Ziel-VA belegt, Branch-Bytes hängen an Payload-Basis | finale Basis + E8 |
| R9 | 3 eingegrenzte In-Funktions-Branches (`0x812494`, `0x850000`, `0x8509c4`) | L (Opcode-Vergleich) | **NIEDRIG** | Ziel `+0x7790` strukturell belegt, keine volle Disassemblierung | E3 |
| R10 | 7 lageunabhängige Stellen (Daten/NOP) | D/N/L | **NIEDRIG** | positionsunabhängig, aber Verhalten noch nicht emuliert | E3/E4/E6 + H3/H4 |
| R11 | `device_status_offset` `0xbbc` | Daten-Offset | **NIEDRIG** | statisch validiert (16 Zugriffe identisch); Hardware-Bestätigung offen | E4 + H6 |
| R12 | GUI Z-Offset (`0x747c8`) | L, UI-Logik | **MITTEL** | einzelner Branch belegt, UI-Gating nur am Gerät prüfbar | E1 + H1 |
| R13 | Kamera (`0x13287c`, pause→resume) | X | **MITTEL** | Callee-Umlenkung belegt, Koexistenz nur am Gerät prüfbar | E2 + H2 |
| R14 | Dual Trust / Reactor / Bootloader | byte-identisch | **NIEDRIG** | unverändert; nur Regression/Boot-Test | E11 + H7 |

## Besonders hervorzuheben (laut Vorgabe)

- **Payload-Referenzen:** R1, R2, R8 — alles hängt an der noch offenen Payload-Basis.
- **Cross-Function-Branches:** R5 (`0x850eb4`, `0x852500`).
- **PLT/GOT-Abhängigkeiten:** R7 (`strcmp`, `std::string`).
- **Stellen mit individuellem Shift (nicht `+0x7790`):** R6 (`+0x2530`), R7 (`-0xd8`, `-0xe4`). Ein Pauschal-Shift ist hier ausdrücklich unzulässig.

## Was HOCH-Risiken auflöst

1. **R1/R2/R8** — Payload über Linker/`objcopy` platzieren, Basis fixieren, dann Gruppe-A- und Gruppe-B-Bytes rechnen und E7/E8 fahren.
2. **R3** — `LanStrategy::start+0x11c` disassemblieren.
3. **R4** — 6 DIFF-Blöcke disassemblieren und Patches neu herleiten.

Erst wenn R1–R4 aufgelöst, alle E-Tests bestanden und die Hardware-Checkliste
(`docs/TESTING.md`) am Drucker grün ist, darf `ported_patch_set=true`.

## Abhängigkeitskette (kurz)

```
Payload-Platzierung (R1)
   └─> Payload-Basis fix
         ├─> Gruppe-A-Bytes (R2)
         └─> Gruppe-B-Bytes (R8) ── R3 (start+0x11c) parallel per Disasm
DIFF-Blöcke (R4) ── unabhängig per Disasm
Alle ──> Emulation (E*) ──> Hardware (H*) ──> ported_patch_set=true
```
