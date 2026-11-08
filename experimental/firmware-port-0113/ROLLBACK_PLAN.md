# Rollback-Plan: Druckerport 02.01.01.13

Konkreter Rückkehrweg zur Referenz-Firmware. **Kein Test ohne funktionierenden
Rollback-Weg.** Dieses Dokument baut keine Verbindung auf; alle Schritte führt der
Nutzer aus, über die legitimen Verfahren in `docs/FIRST_RUN.md` / `docs/INSTALL.md`.

## 1. Zu sichernde Dateien (vor jedem Eingriff)

- Die vollständige Referenz-Firmware (Community V4.4.3, `…zip.sig`), aus der sie
  wiederhergestellt werden kann.
- Die aktuell installierten, zu ändernden Binaries, soweit zugänglich:
  `ec-eeb001-gui`, `elegoo_printer`, `ai_camera` (und bei Gesamtpaket `daemon-000`,
  `libcolib.so`).
- Bootloader-relevante Bereiche nur über das offizielle Paket (nicht einzeln
  patchen) — `uboot`/`boot0` sind Teil des Pakets.
- `/opt/usr`-relevante Konfiguration, soweit sie das Flashen überlebt (laut #309
  überlebt `/opt/usr`, SSH-Host-Key/`authorized_keys` jedoch nicht).

## 2. Prüfsummen vor dem Eingriff

- SHA-256 von Referenz-FW, Basis 02.01.01.13 und Patch-Kandidat notieren.
- SHA-256 jedes Backups direkt nach Erstellung prüfen und mit dem Original
  vergleichen. Ein Backup ohne verifizierte Prüfsumme gilt als nicht vorhanden.

## 3. Wiederherstellung des Originalzustands

1. Gerät in einen definierten Zustand bringen (aus, stabile Stromversorgung).
2. Referenz-Firmware V4.4.3 per USB-Offline-Update flashen (`docs/FIRST_RUN.md`).
3. Nach Neustart Grundfunktion prüfen (Boot, Display, Netzwerk).
4. SSH-Host-Key ist neu (`ssh-keygen -R <host>` auf dem Mac, falls SSH genutzt wird).
5. Verifizieren, dass das Verhalten der Referenz-Baseline (HW-00) entspricht.

## 4. Reihenfolge bei einem fehlgeschlagenen Test

1. **Sofort stoppen**, keine weiteren Änderungen.
2. Zustand sichern: Fotos vom Display, verfügbare Logs, letzte Beobachtung
   (für den Maintainer-Report, siehe §7).
3. Rückkehr zur Referenz-FW (§3).
4. Bestätigen, dass die Referenz wieder sauber läuft.
5. Fehlschlag in `HARDWARE_TEST_CHECKLIST.md` und `TEST_EVIDENCE_TEMPLATE.md`
   dokumentieren, Ursache vermerken.

## 5. Symptome für sofortigen Abbruch

- Boot-Schleife oder kein Boot.
- Watchdog-/Reboot-Loop.
- Abnormale Logs, Kernel-Fehler, Dauer-Fehlermeldungen.
- Überhitzung oder unkontrollierte Heiz-/Bewegungsaktionen.
- Netzwerk dauerhaft weg, kein Zugriff für Recovery.
- Verdacht auf Bootloader-Schaden.

## 6. Schritte je Fehlerbild

| Fehlerbild | Sofortmaßnahme | Rollback |
|---|---|---|
| Boot-Failure / kein Display | Strom trennen, nicht erneut flashen ohne Analyse | Referenz-FW per USB |
| Watchdog-Loop | abwarten, ob Recovery-Menü erreichbar; sonst USB-Offline-Update | Referenz-FW |
| Netzwerkverlust | lokal am Display prüfen; kein Blind-Reflash | Referenz-FW, dann Netzwerk neu |
| GUI-Fehler (Menü/UI) | Funktion notieren, nicht weiterklicken | GUI-Patch zurück / Referenz |
| Druckfehler (Status/Upload) | Druck sicher abbrechen, Heizung/Bewegung stoppen | betroffenen Patch zurück / Referenz |
| Bootloader-Verdacht | **nicht** weiter flashen; Zustand sichern | nur offizielles Paket, ggf. Support |

## 7. Für den späteren Maintainer-/Support-Report sammeln

- Firmware-Version, Patch-Umfang, Datei-Hashes (Original/Referenz/Kandidat).
- Genauer Testfall und Schritt, bei dem der Fehler auftrat.
- Display-Fotos, verfügbare Logs, Boot-Verhalten, Zeitpunkt.
- Ob der Rollback erfolgreich war.
- Vermutete Ursache (ohne Spekulation über nicht Beobachtetes).
- Verweise auf die betroffenen Einträge in `RISK_LIST.md` / `DIFF_BLOCKS.md`.
