# K1-Umstrukturierung — Spezifikation

Arbeitsdokument, nur lokal. Spezifiziert `k1_restructure.py`. Das Skript ist
**strukturell und verhaltensneutral**: Es verschiebt ELF-Segmente/Sektionen und
reserviert den Payload-Bereich (Null-gefüllt). Es schreibt **keine** Code-,
Branch- oder Operanden-Bytes; die Ausgabe ist funktional identisch zur Stock-Binary.
Gruppe-A/B- und DIFF-Branch-Bytes sind **nicht** Teil dieses Schritts.

Konstanten (aus `K1_FEASIBILITY.md`, belegt am neuen Build):
`CUT = 0xcb5000`, `DELTA = 0x10000`, `PAYLOAD_FILE_OFF = 0xcb4fac`,
`PAYLOAD_VA = 0xcc4fac`, `PAYLOAD_SIZE = 500` (`0x1f4`).

## Exakte Liste aller Änderungen

1. **Datei-Splice:** `DELTA` Null-Bytes an Datei-Offset `CUT` einfügen. Neue
   Dateigröße = alt + `DELTA`.
2. **ELF-Header** `e_shoff` (@`0x20`): `+DELTA`.
3. **Programmheader** `p_offset`: `LOAD[4]` (@`0xb8`), `DYNAMIC` (@`0xd8`),
   `PT_TLS` (@`0x118`): je `+DELTA`. VAs unverändert.
4. **`LOAD[3]`** (@`0x94`): `p_filesz` (@`0xa4`) und `p_memsz` (@`0xa8`) auf
   `PAYLOAD_FILE_OFF + PAYLOAD_SIZE = 0xcb51a0` setzen. `p_offset`/`p_vaddr` unverändert.
5. **Section-Header:** jede Sektion mit ursprünglichem `sh_offset >= CUT` und
   `sh_type != NOBITS`: `sh_offset += DELTA` (an der neuen Section-Tabelle bei
   `e_shoff + DELTA`). `sh_addr` aller Sektionen unverändert. Erwartet: genau 10.
6. **Payload-Bereich** `[PAYLOAD_FILE_OFF, PAYLOAD_FILE_OFF + PAYLOAD_SIZE)`:
   mit Payload-Bytes füllen, falls `--payload` angegeben; sonst **Null-gefüllt
   reserviert** (sichere Variante: keine Fremdbytes, keine ausführbare Fremdlogik;
   der Bereich wird erst durch spätere, separate Patches aktiv).

Keine anderen Bytes, Sektionen, Programm- oder Section-Header werden verändert.

## Reihenfolge

Splice → `e_shoff` → 3 phdr-`p_offset` → `LOAD[3]`-Größen → 10 `sh_offset` →
Payload/Reserve schreiben → Nachbedingungen prüfen → erst dann Ausgabedatei gültig.

## Vorbedingungen (vor dem Schreiben)

- Eingabe ist ELF32-LE, ARM.
- Exec-`LOAD[3]` bei `p_offset == 0`.
- `CUT` = kleinster `LOAD`-`p_offset > 0` und `== 0xcb5000`.
- Datei-Lücke `[PAYLOAD_FILE_OFF, CUT)` ist vorhanden und Null.
- Genau 10 Sektionen mit `sh_offset >= CUT` (nicht NOBITS).
- `DELTA % p_align == 0` (`p_align == 0x10000`).
- `--payload` (falls gesetzt) ist exakt `PAYLOAD_SIZE` Bytes.

## Nachbedingungen (nach dem Schreiben, vor Freigabe)

Vollständige Prüfung via `stage0_verify.py` (integriert): Dateigröße = alt+DELTA;
3 phdr-`p_offset` +DELTA, VAs fix; `LOAD[3]` verlängert; `e_shoff` +DELTA; 10
`sh_offset` +DELTA, `sh_addr` fix; Kongruenz aller `PT_LOAD`; Bytes vor altem
Exec-Ende identisch; verschobener Tail identisch; Payload an erwarteter Stelle
(falls gegeben). Nur bei vollständigem `PASS` gilt die Ausgabe als gültig.

## Fehlerfälle

- Vorbedingung verletzt → Abbruch, keine Ausgabe.
- Nachbedingung `FAIL` → Temp-Datei löschen, Exit ≠ 0, klare Meldung.
- Ausgabe existiert ohne `--force` → Abbruch.
- Es bleibt nie eine partielle Ausgabedatei zurück (Schreiben in Temp, atomares
  Umbenennen erst nach `PASS`).

## Rollback-Weg

Die Ausgabe ist eine **neue** Datei; das Original bleibt unberührt. Rückweg =
Original weiterverwenden/wiederherstellen (`ROLLBACK_PLAN.md`). Hashes von Eingabe
und Ausgabe stehen im Report. Am Gerät gilt weiterhin: Software-Rollback ersetzt
keinen physischen Flash-Readback.

## Warum K1 statt K2

Standard-Exec-Segment, Stack bleibt nicht-ausführbar, vorhersagbares Boot-/Rollback-
Verhalten, reproduzierbar per Skript. K2 (GNU_STACK umwidmen) hat Exec-Stack-Risiko
und ist nicht-standard → nur Fallback (`COMPATIBILITY_AND_ROLLBACK.md`, `K1_FEASIBILITY.md`).

## Warum kein Hex-Editor / warum deterministisch

10 Section- und 3 phdr-Felder plus `e_shoff` und der Splice müssen **vollständig
und konsistent** geändert werden; ein vergessenes Feld korrumpiert die Datei. Ein
deterministisches Skript ist reproduzierbar, prüfbar (gleiche Eingabe → gleiche
Ausgabe, gleicher Hash) und auditierbar; ein Hex-Editor ist es nicht.

## Report-Interpretation

Der JSON-Report enthält: Eingabehash, Ausgabehash, `CUT`/`DELTA`, jede geänderte
Stelle mit alt/neu, und jeden Stufe-0-Prüfpunkt mit `PASS`/`FAIL`. Gültig nur, wenn
`overall == "PASS"`. Ein `FAIL` bedeutet: Ausgabe verwerfen, Ursache beheben,
niemals flashen.

## Testergebnis (lokal, Arbeitsverzeichnis)

Ausgeführt gegen die Stock-Binary `elegoo_printer` (`f0111e0b…`), Ausgabe in ein
lokales, git-ignoriertes Arbeitsverzeichnis (nicht im Repo, nicht committet):

- **Stufe 0: 82 PASS, 0 FAIL** (integriert und unabhängig erneut geprüft).
- Dateigröße: `0x1bd23c8` → `0x1be23c8` = **exakt +`0x10000`**.
- **16 geänderte Felder** (erwartet): `e_shoff`; `phdr[4/5/7].p_offset` (LOAD[4],
  DYNAMIC, PT_TLS) je `+0x10000`; `LOAD[3].p_filesz`/`p_memsz` → `0xcb51a0`;
  10 `sh_offset` (`.init_array`, `.fini_array`, `.dynamic`, `.got`, `.data`,
  `.comment`, `.ARM.attributes`, `.shstrtab`, `.symtab`, `.strtab`) je `+0x10000`.
- Alle `sh_addr` und alle Segment-VAs unverändert; Kongruenz für alle `PT_LOAD` erfüllt.
- Exec-Inhalt vor dem alten Ende identisch; verschobener Inhalt byte-identisch
  (um `+0x10000` versetzt); Inhalt hinter der shdr-Tabelle identisch.
- **Deterministisch:** zwei Läufe → identischer Output-SHA-256
  (`3791b1d3…`).
- ELF-Sanity: `file` erkennt eine gültige „ELF 32-bit LSB executable, ARM, …,
  dynamically linked, interpreter /lib/ld-linux-armhf.so.3, not stripped“.
- Payload-Bereich **reserviert (Null)**, da ohne `--payload` ausgeführt.

**Fazit:** Der Stufe-0-Check ist bestanden; die K1-Struktur ist belastbar und
reproduzierbar. Die Umstrukturierung ist verhaltensneutral (keine Code-Bytes
geändert); Branch-Bytes, Emulation und Hardware-Test bleiben die nächsten,
separaten Schritte.
