# Hardware-Test-Runbook: Druckerport 02.01.01.13

Schritt-für-Schritt-Anleitung für die **eigene** Durchführung am Gerät. Dieses
Dokument baut keine Verbindung auf. Flash-/Zugriffswege nur über die im Repo
dokumentierten, legitimen Verfahren (`docs/INSTALL.md`, `docs/FIRST_RUN.md`,
`docs/TESTING.md`, `docs/CAPTURE_VENDOR_PACKAGE.de.md`). Keine Umgehung von
Schutzmechanismen.

## A. Vorbereitung (ohne Gerät)

1. **Versionen/Dateinamen festhalten:** Referenz-Firmware (Community V4.4.3),
   Basis 02.01.01.13, Patch-Kandidat. Exakte Dateinamen notieren.
2. **SHA-256 dokumentieren:** Original, Referenz und Patch-Kandidat.
   ```bash
   shasum -a 256 <datei>
   ```
3. **Backups anlegen:** Original-Firmware, bootloader-relevante Bereiche, alle zu
   ändernden Dateien. Backup-Integrität per SHA-256 verifizieren (siehe
   `ROLLBACK_PLAN.md`).
4. **Rückkehrweg sichern:** dokumentierte Rückkehr zur Referenz-Firmware bereitlegen
   (Offline-Update V4.4.3 per USB, wie in `docs/FIRST_RUN.md`).
5. **Rahmen dokumentieren:** stabile Stromversorgung (kein Flash bei Akku/instabil),
   freier Speicher auf USB-Stick, Zeitbedarf, benötigte Werkzeuge (USB-Stick,
   Kamera/Telefon für Fotos, Notizvorlage `TEST_EVIDENCE_TEMPLATE.md`).
6. **Zugriffswege klären (ohne zu verbinden):** welcher Weg für Backup, Flash,
   Log-Abruf und Recovery nötig ist (USB-Offline-Update; SSH nur auf Community-FW,
   Passwort/Pfad siehe `docs/INSTALL.md`). Nur klären, nicht vorab aufbauen.

## B. Durchführung — Testfall-Schema

Für **jeden** Testfall nach diesem Schema vorgehen (konkrete Fälle in
`HARDWARE_TEST_CHECKLIST.md`, Verhalten in `TEST_MATRIX.md` H1–H7):

- **Voraussetzungen:** welche Gates erfüllt sein müssen (Emulation grün? Payload
  final? Backup vorhanden?).
- **Durchführung Schritt für Schritt:** Flash/Start/Bedienung, einzeln.
- **Erwartetes Verhalten:** konkret, aus der Testmatrix.
- **Beobachtungspunkte:** Display, LEDs, Boot-Dauer, Temperatur, Logs.
- **Zu prüfende Logs/Ausgaben:** CC2-Control-Logs, dmesg/serielles Boot-Log soweit
  verfügbar, App-/Weboberfläche.
- **Pass-Kriterium:** was genau erfüllt sein muss.
- **Fail-Kriterium:** was als Fehlschlag gilt.
- **Abbruchkriterium:** wann sofort gestoppt wird (siehe Sicherheitsregeln).
- **Rückfallmaßnahme:** Verweis auf `ROLLBACK_PLAN.md`.
- **Messwerte/Notizen:** Platz für Fotos, Logs, Beobachtungen (`TEST_EVIDENCE_TEMPLATE.md`).

## C. Sicherheitsregeln

- **Nur eine Änderung pro Test**, außer sie ist ausdrücklich als gemeinsamer
  Testfall definiert. Keine zwei unabhängigen Patches gleichzeitig einführen.
- **Sofort abbrechen** bei: Boot-Problem, Watchdog-/Reboot-Schleife, abnormalen
  Logs, Überhitzung, ungewöhnlichem Verhalten, fehlender Rückkehrmöglichkeit.
- **Kein Test ohne funktionierenden Rollback-Weg.**
- **Ein erfolgreicher Start validiert keine Patches** — jede Funktion einzeln prüfen.
- Bei jedem Abbruch: Zustand und Logs vor dem Rollback sichern (für den
  Maintainer-Report), dann zurück zur Referenz.

## D. Reihenfolge am Gerät (Kurzform)

1. Referenz V4.4.3 → Verhalten dokumentieren (Baseline).
2. GUI-Patch → H1.
3. Kamera-Patch → H2.
4. Drucker lageunabhängig → H3/H4 (Teilmenge), H6.
5. Payload platziert → H5 und Payload-abhängige Funktionen.
6. DIFF-Blöcke → H3/H5 erweitert.
7. Gesamtpatch → H1–H7 vollständig.
8. Stabilität → H7 + Wiederholungen.

Nach jedem Schritt: Ergebnis in `HARDWARE_TEST_CHECKLIST.md` eintragen und einen
`TEST_EVIDENCE_TEMPLATE.md`-Eintrag ausfüllen.
