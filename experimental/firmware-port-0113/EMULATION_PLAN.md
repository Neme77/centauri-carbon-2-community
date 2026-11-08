# Emulationsplan: Druckerport 02.01.01.13

Arbeitsdokument, nur lokal. Dokumentiert die Emulation als **Gate vor dem
Hardware-Test**. Rein lesend/dokumentierend. Keine Patch-Bytes. Grundlage:
`TEST_MATRIX.md` (E1–E11), vorhandene `experimental/firmware-port/test_*44_emulation.py`
(Unicorn führt die echten ARM-Instruktionen aus, Vendor-Funktionen abgefangen).

**Ausdrücklich: „Gerät startet" bzw. „Emulation läuft durch" ist kein
Erfolgskriterium.** Erfolg ist ausschließlich der **korrekte Kontrollfluss /
das korrekte Ziel** je Testfall und Maschinenstatus.

## Grenzen der Emulation

- Emuliert werden einzelne Codepfade mit abgefangenen Vendor-Funktionen; **nicht**
  das Zusammenspiel von HTTP/Netzwerk/MQTT/Kamera am echten Gerät.
- Ein bestandener Emulationstest beweist **keine** Hardware-Sicherheit.
- Die Emulation setzt voraus, dass die jeweiligen Patch-Bytes bereits erzeugt
  wurden (durch den Nutzer); die Emulation prüft sie, erzeugt sie nicht.

## Phasen (nach Verfügbarkeit der Patch-Bytes)

| Phase | Voraussetzung | E-Fälle |
|---|---|---|
| **α — vor Payload-Platzierung** | Nicht-Payload-Patch-Bytes erzeugt | E1 (GUI), E2 (Kamera), E6 (LanStrategy-NOP), E11 (Dual, byte-identisch), E3/E4/E5 (handle_file_upload-Guards + `device_status_offset`), E10-Teil (2 Block-Einbauten `0x812128`/`0x84f940`) |
| **β — nach Payload-Basis** | Payload platziert, Gruppe A/B erzeugt | E7 (Payload-Koexistenz), E8 (Gruppe-B-Branches), E9 (PLT/GOT aus Payload) |
| **γ — nach DIFF-after der 4 Einzel-`bl`** | `AsyncMQTTConnector`/`check_token`-after erzeugt | E10-Rest |

Begründung: Die Emulation führt die gepatchten Instruktionen aus; ein E-Fall ist
erst lauffähig, wenn seine Patch-Bytes existieren. GUI und Kamera sind eigene
Binaries und von der Payload-Platzierung unabhängig.

## Testfälle

Für jeden Fall: **Eingabe**, **erwartetes Verhalten**, **tatsächliches Verhalten**
(auszufüllen), **Logs**, **Pass-Kriterium**, **Fail-Kriterium**, **Abbruch**.

### E1 — GUI Z-Offset (Phase α)
- Eingabe: gepatchte GUI, Einstieg `set_event_cb` case5 `0x747c8`, Druckstatus variiert.
- Erwartet: Sprung nach `0x74bd0` (Z-Offset-Pfad) **unabhängig** vom Status.
- Logs: PC-Verlauf, erreichtes Ziel.
- Pass: Ziel in **jedem** Status erreicht. Fail: statusabhängig gesperrt. Abbruch: PC außerhalb Funktion.

### E2 — Kamera (Phase α)
- Eingabe: gepatchte `ai_camera`, Einstieg `LocalRtcMonitor` `0x13287c`.
- Erwartet: `bl` ruft `resume` (`0x16a7f8`), nicht `pause`.
- Pass: abgefangener Callee = `resume`. Fail: `pause`. Abbruch: unerwarteter Callee.

### E3 — HTTP-Upload-Statuswächter (Phase α)
- Eingabe: gepatchte `elegoo_printer`, Einstiege der `handle_file_upload`-Guards
  (neue VAs), Status 0–31 (wie `test_http44_emulation.py`).
- Erwartet: nur erlaubte Status (1/11 bzw. PRINTING=2) führen fort, sonst Fallthrough.
- Pass: alle 32 Status je Einstieg korrekt. Fail: aktiver Druck fälschlich auf idle. Abbruch: >1 Exit erreicht.

### E4 — `device_status_offset` = `0xbbc` (Phase α)
- Eingabe: Status bei `object+0xbbc`, Stale bei `0xbb4` getrennt geschrieben.
- Erwartet: Entscheidung folgt `0xbbc`, ignoriert `0xbb4`.
- Pass: Verhalten folgt `0xbbc`. Fail: altes Layout `0xbb4` aktiv. Abbruch: inkonsistente Reads.

### E5 — 409 statt Löschen (Phase α)
- Eingabe: existierende Datei, erster Chunk, Nicht-idle-Status.
- Erwartet: HTTP 409 ohne Löschpfad (R4==409).
- Pass: 409 ohne Löschen. Fail: Löschpfad erreicht. Abbruch: Datenpfad nicht erreichbar.

### E6 — LanStrategy::stop ohne HTTP-Stop (Phase α)
- Eingabe: Einstieg `0x826944` (NOP).
- Erwartet: `stop_http_server_file` (`0x827148`) **nicht** aufgerufen.
- Pass: kein Aufruf. Fail: Aufruf erfolgt. Abbruch: unerwarteter Sprung.

### E7 — Payload-Koexistenzlogik (Phase β)
- Eingabe: platzierter Payload (`0xcc4fac`), abgefangene Vendor-Ziele, Modus LAN/WAN.
- Erwartet: korrekter Strategy-/Report-Pfad je Modus.
- Pass: Pfad je Modus korrekt. Fail: falscher Pfad. Abbruch: Sprung ins Leere (Payload falsch platziert).

### E8 — Payload→Vendor-Branches (Phase β)
- Eingabe: Payload-interne Branches (13 Ziele).
- Erwartet: jeder Branch erreicht die belegte Zielfunktion.
- Pass: je Ziel korrekt. Fail: falscher Callee. Abbruch: `start+0x11c` ungelöst → Fall aussetzen.

### E9 — PLT/GOT aus Payload (Phase β)
- Eingabe: Payload-`bl` auf `strcmp`/`std::string`-Stubs.
- Erwartet: Aufruf erreicht libc/libstdc++ über den neuen Stub.
- Pass: Stub = erwartetes Symbol. Fail: falscher/leerer Sprung. Abbruch: Stub außerhalb `.plt`.

### E10 — DIFF-Blöcke (Phase α: 2 Block-Einbauten; Phase γ: 4 Einzel-`bl`)
- Eingabe: gepatchte DIFF-Blöcke, je eigener Guard-/Pfadtest.
- Erwartet: semantisch wie alt.
- Pass: je Block korrekt. Fail: Abweichung. Abbruch: Zielregion unbelegt.

### E11 — Dual Trust / Reactor (Phase α, Regression)
- Eingabe: `daemon-000`/`libcolib.so` (byte-identisch).
- Erwartet: unverändertes Verhalten.
- Pass: Regression grün. Fail: Abweichung. Abbruch: unerwartete Änderung.

## Gate-Logik

- Kein Hardware-Test, bevor die zugehörigen E-Fälle **PASS** sind.
- Ein `FAIL` oder Abbruch in der Emulation stoppt die Weiterarbeit an dieser Stelle,
  bis die Ursache behoben ist (kein Flashen).
- Reihenfolge: Phase α vollständig → β → γ → Hardware (`HARDWARE_TEST_*.md`).

## Was der Nutzer ausfüllt

Pro E-Fall: tatsächliches Verhalten, Logs/Ausgaben, PASS/FAIL, Abweichung,
vermutete Ursache. Vorlage analog `TEST_EVIDENCE_TEMPLATE.md`.
