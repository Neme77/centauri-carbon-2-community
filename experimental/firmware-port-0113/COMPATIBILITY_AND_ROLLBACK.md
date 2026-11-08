# Kompatibilität & Rollback: Druckerport 02.01.01.13

Arbeitsdokument, nur lokal. Lesende Einordnung, keine Patch-Bytes, keine finale
Adresse, kein Gerätezugriff.

Kennzeichnung: **[belegt]** im Repo/per Messung nachgewiesen · **[Nutzerangabe]**
vom Nutzer genannt, hier nicht im Repo belegt · **[Vermutung]** nicht geprüft.

## 1. Kompatibilitätsstatus

- **[belegt]** Community Firmware V4.4.3 ist auf ELEGOO-Basis **02.01.01.07**
  aufgebaut (`experimental/firmware-port/`-Manifeste gegen 02.01.01.07).
- **02.01.01.13** ist eine **neue Basis**. Sie ist als Basis gesichert/gepinnt
  (`builder/current/base_profiles/02.01.01.13.json`), aber **nicht als unterstützte
  Basis der Community Firmware freigegeben**; `ported_patch_set=false`, der Builder
  baut auf dieser Basis fail-closed nicht.
- **Folge:** Unser Drucker-Port ist **experimentell**, nicht kompatibel/release-fähig.
  Es ist eine **Portierungsanalyse** auf die neue Basis, **kein** automatisches
  Upgrade der bestehenden Community Firmware. GUI und Kamera sind hergeleitet und
  verifiziert, aber ebenfalls hardware-ungetestet.

## 2. Rollback-Bedeutung

- **[belegt]** Das Update-Paket ist ein **swupdate-Image** (`.swu`, CPIO newc/crc,
  7 Einträge; `builder/current/core/firmware_builder.py`, `inspect_base.py`).
- **[belegt]** Das Paket enthält `uboot`/`boot0` → der Bootloader wird mitgeschrieben
  (Brick-Risiko bei fehlerhaftem Flash).
- **[Nutzerangabe]** Der CC2 nutze ein **A/B-Schema**, Updates landeten zuerst im
  inaktiven Slot. Im Repo ist ein A/B-Dual-Slot-Schema **nicht belegt** (nur
  swupdate `.swu` ist belegt). Vor Verlass auf A/B am Gerät verifizieren.
- Ein **Software-Rollback ersetzt keinen physischen Flash-Readback**: Nur ein
  Readback belegt, dass die Bytes am Gerät den erwarteten entsprechen.
- **Rollback ist Pflicht vor jedem Hardware-Test, nicht optional** (`ROLLBACK_PLAN.md`).

## 3. Risikoklassen

| Klasse | Umfang | Stufe | Bezug `RISK_LIST.md` |
|---|---|---|---|
| **A** | reine Datenpatches, NOPs, lageunabhängige Stellen | niedrig | R9, R10, R11, R14 |
| **B** | Payload-abhängige Referenzen, Cross-Function-Branches | hoch | R2, R5, R8 |
| **C** | DIFF-After-Operanden, PLT-/GOT-Abhängigkeiten | hoch | R4, R7, R3 |
| **D** | Kernel, Bootloader, Partitionstabelle, kritische Systemkomponenten | höchstes — **nur mit Maintainer-Freigabe** | (Paket-/Bootloader-Ebene) |

Klasse D ist durch unseren Port nicht geändert (`uboot`/`boot0` identisch,
`daemon-000`/`libcolib.so` byte-identisch), bleibt aber durch das Mitschreiben des
Pakets risikobehaftet und erfordert Maintainer-Freigabe.

## 4. Rollback-Gates

- **Vor jedem Hardware-Test:** Original-Hashes, Backup-Pfade und exakter
  Wiederherstellungsweg dokumentiert und **geprüft** (`ROLLBACK_PLAN.md` §1–§3).
- **Vor Payload-Platzierung:** Rollback-Plan für **K1 und K2** bewertet (siehe §5);
  die gewählte Variante muss einen reproduzierbaren Rückweg haben.
- **Vor DIFF-After-Tests:** Rollback für **alle** betroffenen Stellen nachvollziehbar.
- **Kein Hardware-Test ohne geprüften, reproduzierbaren Wiederherstellungsweg.**

## 5. K1 vs. K2 — Entscheidungsmatrix

Datenbasis: `README.md`, Abschnitt „Payload placement“ (ELF-Layout des neuen Builds).

| Kriterium | K1 (LOAD[4]+Sektionen +0x10000 verschieben, LOAD[3] verlängern) | K2 (GNU_STACK → PT_LOAD umwidmen, am Ende anhängen) |
|---|---|---|
| Freier Platz & Alignment | schafft 64 KB durch `+0x10000`-Verschiebung; `p_align 0x10000` gewahrt, Kongruenz ok | additiv, frische VA; Kongruenz der neuen VA/Offset selbst herstellen |
| Konflikte mit Segmenten | berührt `LOAD[4]`, `DYNAMIC`, `GNU_RELRO`, Sektionstabelle — viele, aber mechanisch | keine Verschiebung bestehender Segmente; nutzt den Null-Slot `GNU_STACK` |
| ELF-/Toolchain-Änderungen | groß: `e_shoff` + alle `sh_offset ≥ 0xcb5000` + 3 Programmheader anpassen; Linker/`objcopy` | kleiner Eingriff an der Segmenttabelle, aber **nicht-standard** |
| Relokationsaufwand Gruppe A | Payload nahe `.text` (`0xcc4fac`), Offsets einfach | frische hohe VA, Offsets einfach (Reichweite prüfen) |
| Relokationsaufwand Gruppe B | unverändert (Zielfunktionen fix; nur Quelladressen durch neue Basis) | unverändert |
| Auswirkung auf Rollback/Wiederherstellung | Standard-Image, Rollback = normales Reflash; vorhersagbar | **Exec-Stack-Risiko**; Vendor-Loader könnte abweichende Segmenttabelle ablehnen → Boot-/Rollback-Risiko höher |
| Wartbarkeit & Reproduzierbarkeit | Standard-Exec-Segment, Stack bleibt non-exec; gut reproduzierbar | nicht-standard, Exec-Stack-Regression; schlechter wartbar |

### Empfehlung (vorläufig, nicht final)

**K1 wird bevorzugt.** Begründung über die geforderten Achsen:
- **Risiko:** niedriger — Standard-Exec-Segment, Stack bleibt nicht-ausführbar,
  vorhersagbares Boot-/Rollback-Verhalten; kein nicht-standard Segment, das ein
  Vendor-Loader ablehnen könnte.
- **Relokationsaufwand:** für Gruppe A/B praktisch gleich zu K2; K1 legt den Payload
  sogar näher an `.text`.
- **Wartbarkeit/Reproduzierbarkeit:** höher — standardkonformes Layout, mit
  Linker/`objcopy` reproduzierbar statt handgebauter Segmenttabelle.
- **Rollback:** sauberer, da gewöhnliches Image ohne Sonderannahmen.

K1 erkauft das mit **höherem einmaligem Toolchain-Aufwand** (Sektionstabelle
verschieben). **K2** bleibt die **additive Rückfalloption**, falls der K1-Toolchain-Schritt
praktisch nicht machbar ist — dann mit ausdrücklicher Bewertung des Exec-Stack-Risikos.
**Keine finale Festlegung**; die Wahl trifft der Maintainer mit der Toolchain.

## 6. Reihenfolge für die spätere Umsetzung

```
Payload-Basis fixieren (K1 bevorzugt)
   → Gruppe A (Referenzen in den Payload)
   → Gruppe B (Payload-interne Vendor-Branches; start+0x11c per Disasm)
   → DIFF-after (Operanden neu kodieren)
   → Emulation (TEST_MATRIX.md, E*)
   → Hardware-Test (HARDWARE_TEST_*.md, H*)
```

Jede Stufe hat ihr Rollback-Gate (§4); keine Stufe gilt als getestet, bevor ihre
Voraussetzungen erfüllt und per Emulation bestätigt sind.
