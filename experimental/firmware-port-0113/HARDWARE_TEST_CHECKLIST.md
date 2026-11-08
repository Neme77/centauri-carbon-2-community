# Hardware-Test-Checkliste: Druckerport 02.01.01.13

Prüfbare Checkliste. Status leer lassen, bis am Gerät getestet. Verhalten/Details
in `TEST_MATRIX.md` (H1–H7) und `HARDWARE_TEST_RUNBOOK.md`. **Voraussetzung**-Spalte
nennt das Gate; ein Test ohne erfülltes Gate bleibt `BLOCKED`.

Status-Werte: `offen` · `BLOCKED` (Gate nicht erfüllt) · `PASS` · `FAIL` · `INCONCLUSIVE`.

| Test-ID | Bereich | Voraussetzung | Testziel | Erwartetes Ergebnis | Pass-Kriterium | Fail-Kriterium | Status | Ergebnis / Notiz |
|---|---|---|---|---|---|---|---|---|
| HW-00 | Referenz-Baseline | Referenz-FW geflasht, Backup vorhanden | Ausgangsverhalten dokumentieren | normale Funktion | alle Grundfunktionen normal | Referenz fehlerhaft → Stopp | | |
| HW-01 | Boot & Systemstart | Patch-Kandidat, Backup, Rollback bereit | sauberer Boot ohne Schleife | CC2 Control startet überwacht | sauberer Boot, kein Watchdog-Loop | Boot-Schleife/Brick | | |
| HW-02 | GUI-Z-Offset | GUI-Patch, Emulation E1 | Z-Offset-Menü im Druck erreichbar | Menü öffnet, kein Absturz | Menü erreichbar in allen Status | gesperrt / UI hängt | | |
| HW-03 | Kamera | Kamera-Patch, E2 | lokaler + Remote-Stream koexistieren | beide Streams laufen | lokal bleibt bei WAN-RTC | lokaler Stream bricht | | |
| HW-04 | Drucker-Grundfunktionen | lageunabhängige Stellen | Heizen, Lüfter, Bewegung | normal wie Referenz | alle Kernfunktionen normal | Fehlfunktion | | |
| HW-05 | LAN-Verhalten | Netzwerk-Patch + Payload final | LAN-Betrieb/Reconnect | stabil | Verbindung stabil | Verbindungsfehler | | |
| HW-06 | WLAN-Verhalten | wie HW-05 | WLAN-Betrieb/Wechsel | stabil | stabil, Wechsel sauber | Reconnect-Schleife | | |
| HW-07 | HTTP-/Weboberfläche | HTTP-Patch, E3/E5 | Weboberfläche bedienbar | normale Bedienung | alle Seiten funktionieren | Fehler/Timeout | | |
| HW-08 | MQTT | Netzwerk+Payload, E7 | MQTT-Reconnect ohne Neustart | Reconnect sauber | stabil | Schleife/Abbruch | | |
| HW-09 | Datei-Upload | HTTP-Patch, E3/E5 | Upload in Idle und Druck | Status korrekt, kein Datenverlust | korrektes Statusverhalten | Druck verworfen/Datenverlust | | |
| HW-10 | Token-/Auth-Pfad | `check_token` DIFF neu kodiert, Payload | Token-Prüfung LAN/WAN | korrekt zugelassen/abgelehnt | Verhalten wie spezifiziert | falsche Zulassung/Ablehnung | | |
| HW-11 | AsyncMQTTConnector | DIFF `0x837168/0x8371a0` neu kodiert | MQTT-Task-Namen LAN/WAN | korrekte Taskbenennung | wie spezifiziert | Fehlverhalten | | |
| HW-12 | `set_mode` | DIFF `0x812128` neu kodiert, Payload | Moduswechsel LAN/WAN | sauberer Wechsel | stabil | Hänger/Absturz | | |
| HW-13 | `handle_file_upload` | DIFF `0x84f940` neu kodiert | Upload-Statuslogik, 409 | korrektes Statusverhalten, 409 ohne Löschen | wie spezifiziert | Datei gelöscht/falscher Status | | |
| HW-14 | `check_token` | DIFF neu kodiert | Tokenabgleich | korrekt | wie spezifiziert | Fehlverhalten | | |
| HW-15 | `LanStrategy` | NOP/Netzwerk-Patch | HTTP-Server-Verhalten im LAN-Stop | HTTP bleibt wie vorgesehen | wie spezifiziert | HTTP fälschlich gestoppt | | |
| HW-16 | Payload-abhängige Referenzen | Payload-Basis final, Gruppe A/B neu kodiert, E7/E8 | Koexistenzpfade | korrekter Pfad je Modus | wie spezifiziert | falscher Pfad/Absturz | | |
| HW-17 | DIFF-Blöcke (gesamt) | alle DIFF-After neu kodiert, E10 | Verhalten der 6 Blöcke | wie Referenz-Semantik | alle Blöcke korrekt | Abweichung | | |
| HW-18 | Druckauftrag starten | Gesamtpatch | Start eines Drucks | startet normal | sauberer Start | Fehlstart | | |
| HW-19 | Druck pausieren/abbrechen | Gesamtpatch | Pause/Cancel-Guards | korrektes Verhalten | Status korrekt | Fehlverhalten | | |
| HW-20 | Neustart/Wiederanlauf | Gesamtpatch | Power-Cycle-Persistenz | sauberer Wiederanlauf | stabil über Zyklen | Instabilität | | |
| HW-21 | Rückkehr zur Referenz | Backup/Rollback | Rollback funktioniert | Referenz-FW läuft wieder | sauberer Rückflash | Rollback schlägt fehl | | |

Hinweis: HW-05/06/08/10–14/16/17 sind `BLOCKED`, bis Payload-Basis final **und**
die zugehörigen After-/DIFF-Bytes erzeugt und emulationsgeprüft sind.
