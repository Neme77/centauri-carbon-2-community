# Test-Matrix: Port 02.01.01.07 → 02.01.01.13

Arbeitsdokument, nur lokal. Plant Emulations- und Hardware-Tests für den Port.
**Keine Patch-Bytes, keine Adressen final.** Adressen sind die aus der
lesenden Analyse belegten Werte (siehe `README.md`); sie werden im Test
bestätigt, nicht vorausgesetzt.

Grundlagen:
- Emulation nach Vorbild `experimental/firmware-port/test_*44_emulation.py`
  (Unicorn führt die echten ARM-Instruktionen aus, Vendor-Funktionen werden
  abgefangen, Kontrollfluss pro Maschinenstatus geprüft; schreibt Status nach
  `object+0xbbc` und einen Stale-Wert nach `0xbb4`).
- Hardware nach `docs/TESTING.md`, Abschnitt „Real-printer release checklist“.
- Ein bestandener Host-/Emulationstest beweist **keine** Hardware-Sicherheit.

## Patch-Kategorien

| Kat. | Bedeutung | Neuberechnung nach Verschiebung? |
|---|---|---|
| D | Reiner Datenpatch (`cmp`/`mov`/`ldr [r11,#…]`) | nein |
| N | NOP (Aufruf entfällt) | nein |
| L | Lageunabhängig / In-Funktion, Ziel `+0x7790` belegt | nein |
| P | Payload-abhängig (Ziel im Block `0xcd77e8`) | ja, nach Payload-Platzierung |
| X | Cross-Function-Branch (Ziel in anderer Funktion) | ja |
| G | PLT/GOT-Referenz | ja (neuer Stub) |
| F | DIFF-Block (rekompiliert) | ja, Disassemblierung |

## A. Emulations-Matrix

| # | Ziel | Betroffene Stelle | Kat. | Erwartetes Verhalten | Beobachtung | Pass/Fail | Rückfall bei Fail |
|---|---|---|---|---|---|---|---|
| E1 | GUI Z-Offset im Druck erreichbar | `set_event_cb` case5 @ `0x747c8` → `0x74bd0` | L | Sprung in den Z-Offset-Pfad unabhängig vom Druckstatus | PC landet bei `0x74bd0` (→ `setting_set_z_offset_create`) | PASS wenn Ziel erreicht, in jedem Status | Menü bleibt statusabhängig gesperrt |
| E2 | Kamera-Koexistenz | `LocalRtcMonitor` @ `0x13287c` | X | `bl` ruft `resume` (`0x16a7f8`), nicht `pause` | abgefangener Callee = `resume` | PASS wenn `resume` | lokaler MJPEG bricht bei WAN-RTC ab |
| E3 | HTTP-Upload Statuswächter | `handle_file_upload` Einsprünge `0x857100`/`0x8579c8`/`0x858138`/`0x857140` → `set_status`/`set_sub_status` (neue VAs) | L/X | nur erlaubte Status (1/11, bzw. PRINTING=2) führen fort, sonst Fallthrough | wie `test_http44_emulation.py`: pro Status 0–31 landet PC am erwarteten Ziel | PASS wenn alle 32 Status pro Einsprung korrekt | aktiver Druck wird fälschlich auf idle gezwungen |
| E4 | `device_status_offset` = `0xbbc` | Member-Zugriff in den HTTP-Statuswächtern | D | Status wird bei `object+0xbbc` gelesen, Stale `0xbb4` ignoriert | Test schreibt `0xbbc`/`0xbb4` getrennt; Verhalten folgt `0xbbc` | PASS wenn Entscheidung `0xbbc` folgt | altes Layout `0xbb4` aktiv → falscher Status |
| E5 | 409 statt Löschen bei Erst-Chunk | `handle_file_cancel`/`handle_file_upload` Reclaim | F | existierende Datei → HTTP 409, keine Löschung | R4==409 im Nicht-idle-Fall (wie bestehender Test) | PASS wenn 409 ohne Löschpfad | Datei wird gelöscht |
| E6 | LanStrategy::stop ohne HTTP-Stop | `0x826944` NOP | N | `stop()` ruft `stop_http_server_file` **nicht** | kein Aufruf des abgefangenen `0x827148` | PASS wenn kein Aufruf | HTTP-Server wird im LAN-Stop beendet |
| E7 | Payload-Koexistenzlogik | Block `0xcd77e8` (neue Position) | P | LAN/WAN-Koexistenz: korrekte Strategy-/Report-Pfade je Modus | Payload mit abgefangenen Vendor-Zielen ausführen, Pfad pro Modus prüfen | **blockiert** bis Payload platziert | — |
| E8 | Payload→Vendor-Branches | 13 Gruppe-B-Ziele | P/X/G | jeder Branch erreicht die belegte Zielfunktion | abgefangene Callees == erwartete Funktion | PASS je Ziel; `start+0x11c` offen | falscher Callee |
| E9 | PLT/GOT aus Payload | `strcmp`, `std::string`-Stubs | G | Aufruf erreicht libc/libstdc++ über neuen Stub | abgefangener Stub == erwartetes Symbol | PASS je Stub | Sprung ins Leere/falsche Funktion |
| E10 | DIFF-Blöcke | `set_mode`, `handle_file_upload`-Block, `AsyncMQTTConnector`, `check_token` | F | nach Neuherleitung: semantisch wie alt | pro Block eigener Guard-Test | **blockiert** bis disassembliert | — |
| E11 | Dual Trust / Reactor (byte-identisch) | `daemon-000`, `libcolib.so` | — | unverändertes Verhalten | Regression der bestehenden Dual-Tests | PASS wenn unverändert | — |

## B. Hardware-Matrix (`docs/TESTING.md`)

| # | Ziel | Betroffener Patch | Erwartetes Verhalten | Beobachtung | Pass/Fail | Rückfall |
|---|---|---|---|---|---|---|
| H1 | Z-Offset-Menü im laufenden Druck | GUI | Menü öffnet auch während des Drucks | Bedienung am Display | PASS wenn erreichbar, kein Absturz | gesperrt / UI hängt |
| H2 | Kamera + Remote gleichzeitig | Kamera | lokaler MJPEG bleibt, WAN-RTC steuert | Stream lokal + Cloud | PASS wenn beide laufen | lokaler Stream bricht |
| H3 | OrcaSlicer Upload / Start / Cancel | HTTP | Upload in Idle und Druck korrekt; Cancel-Guards | Upload großer Datei, Abbruch | PASS wenn Status korrekt, kein Datenverlust | Upload verwirft aktiven Druck |
| H4 | Dateiname mit Leerzeichen, Mehrfach-Ops | HTTP | korrekte Behandlung | mehrere Dateien | PASS wie Stock | Fehler/Hänger |
| H5 | LAN/WAN-Moduswechsel, MQTT-Koexistenz | Netzwerk + Payload | Wechsel ohne Serviceneustart | MQTT/Canvas Reconnect | PASS wenn stabil | Reconnect-Schleife |
| H6 | Gerätestatus-Meldungen | `device_status_offset` | korrekte Status/Substatus | Statusanzeige/Logs | PASS wenn plausibel | falscher Status |
| H7 | Boot, Rollback, Power-Cycle | uboot/boot0 + Gesamtpaket | sauberer Boot, Rollback möglich | Power-Cycle, Update/Downgrade | PASS wenn kein Brick | — (Abbruch, nicht flashen) |

## Reihenfolge

1. E1, E2, E6 (lageunabhängig/einfach) — sofort emulierbar.
2. E3, E4, E5 (`test_http44_emulation.py` auf neue Adressen anpassen).
3. E11 Regression.
4. E7–E10 erst nach Payload-Platzierung bzw. DIFF-Disassemblierung.
5. Hardware H1–H7 zuletzt, nach bestandener Emulation.

## Offene Abhängigkeiten

- E7, E8 (teilw.), E10 hängen an Payload-Platzierung bzw. Disassemblierung.
- `LanStrategy::start+0x11c` (Gruppe B) bleibt bis zur Disassemblierung offen.
- Alle Hardware-Tests setzen einen vollständig portierten, emulations­geprüften
  Patch-Satz voraus.
