# K1-Machbarkeit (Phase 1) & K2-Risiko (Phase 2)

Arbeitsdokument, nur lokal. Lesende Analyse + Methoden-Spezifikation. **Keine
Patch-Bytes, keine finale Payload-Basis, kein Binär-modifizierendes Skript wird
hier ausgeführt.** Die Struktur-Offsets unten sind ELF-Layout-Analyse des neuen
Builds (`f0111e0b…`), keine Patch-Operanden.

Grundprinzipien (unverändert): Stock-Kernel bleibt; keine Änderung an Bootloader,
Partitionstabelle oder kritischen Komponenten; strukturelle Änderungen nur über
ein **deterministisches Skript / Toolchain**, **nie** per Hex-Editor;
`ported_patch_set=true` erst nach Emulation + Hardware-Test.

---

## Phase 1 — K1-Machbarkeit

K1 = 500-Byte-Payload ins reguläre ausführbare Segment `LOAD[3]` integrieren,
indem alles ab Datei-Offset `0xcb5000` um `+0x10000` verschoben und `LOAD[3]`
verlängert wird.

### 1.1 Betroffene Felder (belegt aus dem neuen Build)

**Programmheader (phdr):**

| Segment | phdr@ | Feld | alt | Aktion |
|---|---|---|---|---|
| `LOAD[3]` (R-X) | `0x94` | `p_filesz` @`0xa4` | `0xcb4fac` | verlängern auf Payload-Ende |
| `LOAD[3]` (R-X) | `0x94` | `p_memsz` @`0xa8` | `0xcb4fac` | verlängern (= filesz) |
| `LOAD[4]` (RW) | `0xb4` | `p_offset` @`0xb8` | `0xcb5000` | `+0x10000` → `0xcc5000`; `p_vaddr 0xcd5000` **bleibt** |
| `DYNAMIC` | `0xd4` | `p_offset` @`0xd8` | `0xcb51f8` | `+0x10000` → `0xcc51f8`; `p_vaddr` bleibt |
| Typ-7 (`PT_TLS`) | `0x114` | `p_offset` @`0x118` | `0xcb5000` | `+0x10000` → `0xcc5000`; `p_vaddr` bleibt |

**ELF-Header:** `e_shoff` @`0x20` = `0x1bd1f18` → `+0x10000` = `0x1be1f18`.

**Section-Header (10 von 30 mit `sh_offset ≥ 0xcb5000`, je `sh_offset += 0x10000`):**
`.init_array` (`0xcb5000`), `.fini_array` (`0xcb51f4`), `.dynamic` (`0xcb51f8`),
`.got` (`0xcb5388`), `.data` (`0xcb5cd0`), `.comment` (`0xcbbe78`),
`.ARM.attributes` (`0xcbbeac`), `.symtab` (`0xcbbee4`), `.strtab` (`0x10cbc84`),
`.shstrtab` (`0x1bd1e18`). Die `sh_addr` (VA) der geladenen Sektionen **bleibt**;
nur `sh_offset` verschiebt sich. Die übrigen 20 Sektionen (`sh_offset < 0xcb5000`)
bleiben unverändert.

### 1.2 Kongruenzbedingung

`p_offset ≡ p_vaddr (mod p_align)` mit `p_align = 0x10000` muss erhalten bleiben.
Weil das Delta **`+0x10000`** ein Vielfaches von `p_align` ist und die VAs fix
bleiben, bleibt die Kongruenz für `LOAD[4]`/`DYNAMIC`/`TLS` automatisch gewahrt
(Beispiel `LOAD[4]`: `0xcc5000 mod 0x10000 = 0x5000 = 0xcd5000 mod 0x10000`). Für
`LOAD[3]` ändert sich nur die Länge, nicht der Start → Kongruenz unberührt.

### 1.3 Sichere Reihenfolge (deterministisch)

1. **Datei-Splice:** `0x10000` Null-Bytes an Datei-Offset `0xcb5000` einfügen
   (alles ab dort rückt um `0x10000` nach hinten). Reine Dateioperation.
2. `LOAD[4]`, `DYNAMIC`, `TLS`: `p_offset += 0x10000`.
3. `e_shoff += 0x10000`.
4. Für die 10 Sektionen: `sh_offset += 0x10000` (an der neuen Section-Tabelle).
5. Payload (500 B) an Datei-Offset `0xcb4fac` (VA `0xcc4fac`) in den freigewordenen
   Bereich schreiben; Rest bis `0xcc5000` bleibt Null-Padding.
6. `LOAD[3]`: `p_filesz` und `p_memsz` auf das Payload-Ende verlängern.
7. Gegenprüfung (Stufe 0) **vor** jedem weiteren Schritt.

> `PAYLOAD_BASE_NEW` = VA `0xcc4fac` ist hier **nur Kandidat**, nicht final. Die
> Festlegung erfolgt erst in Phase 3 nach bestandener Machbarkeit.

### 1.4 Toolchain vs. Hex-Editor

- **Deterministisches Skript (pyelftools/`struct`) oder Linker-Rewrite:** Splice,
  phdr-/shdr-Offsetanpassung, `e_shoff`, Segmentverlängerung. Nur so sind die 10
  Section- und 3 phdr-Änderungen vollständig und reproduzierbar.
- **`objcopy` allein genügt nicht**: `--add-section` kann einen Abschnitt anfügen,
  aber nicht zuverlässig in ein bestehendes ausführbares `PT_LOAD` einbetten und
  dessen `p_filesz`/`p_memsz` kohärent erweitern.
- **Nicht per Hex-Editor:** die Section-Tabellen-Updates und der Splice — zu
  fehleranfällig (ein vergessener `sh_offset` korrumpiert die Datei).

### 1.5 K1-Risiken

| Frage | Bewertung |
|---|---|
| Kann `LOAD[4]` sicher verschoben werden? | Ja, wenn **alle** zugehörigen `sh_offset` konsistent `+0x10000` und die VAs fix bleiben. Risiko nur bei unvollständiger Anpassung → daher Skript, kein Hex-Editor. |
| Bleibt `DYNAMIC` konsistent? | Ja: `PT_DYNAMIC.p_offset` + `.dynamic`-`sh_offset` verschieben gemeinsam; `.got` referenziert VAs (unverändert). |
| Bleibt `GNU_RELRO` konsistent? | Dieser Build hat **kein** `PT_GNU_RELRO`. Stattdessen ein Typ-7-Segment (`PT_TLS`), das über seinen `p_offset` mitverschoben wird. |
| Bleibt die Section-Tabelle konsistent? | Ja bei vollständigem `sh_offset += 0x10000` für die 10 Sektionen + `e_shoff`. |
| Signatur/Hash/Manifest? | Der Builder verpackt neu und **signiert** mit dem Community-Key; der Stock-Hash im Base-Profil pinnt den **Input** (Stock `f0111e0b`), nicht den gepatchten Output. Die ELF-Umstrukturierung bricht keine Signatur, solange über den Builder neu verpackt wird. |
| Boot-/Brick-Risiko | Primär bei inkonsistenten Offsets oder wenn der Loader das Layout strikt prüft. Minderung: deterministisches Skript + Stufe-0-Verifikation + Emulation + geprüfter Rollback. Bootloader/Partitionen werden **nicht** angefasst. |

### 1.6 K1-Rollback

- **Sichern:** Original-`elegoo_printer` (neu, Stock), vollständiges Referenzpaket
  (Community V4.4.3), alle zu ändernden Dateien (`ROLLBACK_PLAN.md` §1).
- **Hashes vor/nach:** SHA-256 von Stock-Input, gepatchtem Output und Backup; nach
  dem Test Readback-Hash (soweit zugänglich) gegen Erwartung.
- **Wiederherstellungsweg:** Referenz-FW V4.4.3 per USB-Offline-Update
  (`docs/FIRST_RUN.md`), danach Baseline-Verhalten (HW-00) bestätigen.
- **Beleg für erfolgreichen Rollback:** sauberer Boot + Referenz-Baseline-Verhalten
  + passende Hashes der wiederhergestellten Dateien.
- **Abbruch/Eskalation:** Boot-Schleife, Watchdog-Loop, inkonsistente Hashes,
  Bootloader-Verdacht → sofort stoppen, Zustand sichern, nicht weiter flashen.
- **Ausdrücklich:** Ein Software-Rollback ersetzt **keinen** physischen
  Flash-Readback; nur ein Readback belegt die tatsächlichen Bytes am Gerät.

---

## Phase 2 — K2 als Fallback (Risikobewertung)

K2 = `GNU_STACK` (Null-Slot) zu `PT_LOAD (R-X)` umwidmen, Payload am Dateiende
anhängen, frische VA.

- **Annahmen über den Vendor-Loader:** (a) lädt alle `PT_LOAD`-Segmente unabhängig
  von ihrer Zahl/Reihenfolge; (b) benötigt kein `PT_GNU_STACK` zur Stack-Konfiguration;
  (c) prüft die Segmenttabelle nicht gegen eine gespeicherte Referenz/Signatur der
  Struktur.
- **Risiken:**
  - **Exec-Stack-Regression:** Fehlt `PT_GNU_STACK`, markieren manche Loader/Kernel
    den Stack als ausführbar → Sicherheitsregression.
  - **Nicht-standard:** ein aus `GNU_STACK` umgewidmetes `PT_LOAD` ist unüblich; ein
    strikter Loader könnte es ignorieren oder ablehnen → Boot-Risiko.
  - Signatur wie bei K1 unkritisch (Builder signiert neu).
- **Nötige Prüfungen, bevor K2 als Fallback gilt:**
  1. Emulation/Boot-Test belegt, dass der Loader das neue `PT_LOAD` tatsächlich
     ausführbar mappt.
  2. Stack-Exec-Verhalten auf diesem Kernel geprüft (bleibt nicht-ausführbar?).
  3. Keine Secure-Boot-/phdr-Zählungs-Prüfung lehnt die Struktur ab.
- **Bedingungen, damit K2 nicht automatisch abgelehnt wird:** alle drei Prüfungen
  bestanden **und** das Exec-Stack-Risiko entweder ausgeschlossen oder bewusst
  akzeptiert/gemindert. Andernfalls bleibt nur K1.

---

## Phasen-Zusammenfassung

**Neu belegt:**
- Exakte K1-Feldliste: 3 phdr-`p_offset`, `e_shoff`, 10 `sh_offset`, `LOAD[3]`
  `p_filesz`/`p_memsz`; Delta `+0x10000` wahrt die `p_align`-Kongruenz.
- Der Build hat **kein** `PT_GNU_RELRO`; betroffen ist ein `PT_TLS` (Typ 7).
- K1 ist mit einem deterministischen Skript/Toolchain-Schritt **strukturell
  machbar**; `objcopy` allein genügt nicht; Hex-Editor ist auszuschließen.
- K2-Annahmen und -Prüfbedingungen sind definiert; K2 bleibt Fallback mit
  Exec-Stack-Vorbehalt.

**Noch offen:**
- Ausführung der Umstrukturierung (Binär-modifizierend) — Maintainer/Toolchain.
- Phase 3 (finale `PAYLOAD_BASE_NEW`, Gruppe A/B, DIFF-after, 9 After-Stellen),
  Phase 4 (Emulation), Phase 5 (Hardware) — nach bestandener Machbarkeit.

**Risiken:**
- Unvollständige Offset-Anpassung (K1) → Korruption; Minderung per Skript + Stufe 0.
- Exec-Stack/Loader-Ablehnung (K2) → nur nach Prüfung einsetzbar.
- Boot-/Brick-Restrisiko bei jedem Flash → Rollback-Gate zwingend.

**Nächster konkreter Schritt:**
- K1 als Arbeitsbasis bestätigen und ein **deterministisches Umstrukturierungs-Skript**
  spezifizieren/implementieren (Maintainer-Schritt, Binär-modifizierend). Als rein
  lesende Vorbereitung kann ein **Stufe-0-Verifizierer** (prüft Offsets, Kongruenz,
  Section-Konsistenz, keine unbeabsichtigten Überschreibungen) erstellt werden —
  er schreibt nichts und prüft nur eine bereits umstrukturierte Datei.
