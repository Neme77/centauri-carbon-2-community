# Hardware-Testplan: Druckerport 02.01.01.13

Arbeitsdokument, nur lokal. Plant die Hardware-Testphase. **Alle Hardware-Schritte
führt der Nutzer selbst am Gerät aus; dieses Dokument baut keine Verbindung auf
und enthält keine Patch-Bytes.**

## Freigabe-Bedingungen (Gates) vor jedem Hardware-Test

Hardware-Tests beginnen **erst**, wenn:
1. Die Payload-Basis final platziert und verifiziert ist (`README.md`, „Payload
   placement“; Kandidaten K1/K2).
2. Die betroffenen After-Operanden / Branch-Bytes erzeugt **und** verifiziert sind.
3. Die Emulation (`TEST_MATRIX.md`, E-Fälle) für den jeweiligen Bereich bestanden ist.

Ein Bereich ohne erfüllte Voraussetzung wird **nicht** am Gerät getestet.

## Statusvokabular (strikt getrennt)

| Status | Bedeutung |
|---|---|
| **Patch vorhanden** | Byte-Änderung existiert, sonst nichts belegt |
| **statisch plausibel** | Adressen/Shift/before-Fenster lesend belegt (aktueller Analysestand) |
| **Emulation bestanden** | Codepfad in Unicorn korrekt (E-Fall grün) |
| **Hardware getestet** | am realen Drucker bestätigt (H-Fall grün) |

Ein startendes Gerät beweist **keinen** dieser höheren Status. „Hardware getestet“
gilt nur für den konkret geprüften Bereich, nie pauschal.

## Testziele

- Gerät startet mit dem vorbereiteten Port; keine Boot-Schleife, kein Brick, keine
  dauerhafte Funktionsbeeinträchtigung.
- GUI-Z-Offset-Änderung verifiziert.
- Kamera-Änderung verifiziert.
- Drucker-seitige Stellen verifiziert, zuerst die lageunabhängigen.
- Payload-abhängige Stellen **erst nach** Payload-Platzierung + Neukodierung.
- DIFF-Blöcke **erst nach** Abschluss ihrer After-Neukodierung.
- Keine Regression bei Netzwerk, HTTP, MQTT, Upload, Token.

## Teststufen

- **Stufe 0 — Vorabprüfung ohne Gerät.** Prüfsummen, Dateigrößen, ELF-/Programm-Header,
  Segmentgrenzen, Alignment, Offset-Kongruenz; vollständige Gegenprüfung aller
  geplanten Byte-Änderungen; Bestätigung, dass keine unbeabsichtigten
  Überschreibungen vorliegen (insb. `LOAD[4]`/Sektionen bei Payload-Platzierung).
- **Stufe 1 — Isolierte Emulation.** E-Fälle der Testmatrix. Hardware ersetzt
  Emulation nicht.
- **Stufe 2 — Kontrollierter Hardware-Test.** Mit Backup, definiertem Rollback und
  Beobachtungspunkten; zuerst minimal-invasive Änderungen, dann abhängige Stellen.
- **Stufe 3 — Funktionale Validierung.** GUI, Kamera, Netzwerk, Druckauftrag,
  Statusmeldungen, Upload, MQTT, Token, Drucker-Kernfunktionen.
- **Stufe 4 — Stabilität.** Wiederholte Starts, längere Laufzeit, Netzwerkwechsel,
  Druckabbruch und erneuter Start.

## Testreihenfolge (mit Begründung)

| # | Schritt | Begründung |
|---|---|---|
| 1 | Unveränderte Referenz-Firmware (Community V4.4.3) testen, Verhalten dokumentieren | Basiswert; ohne Referenz kein Regressionsvergleich |
| 2 | Nur GUI-Patch | kleinste, abgeschlossene Änderung (1 Stelle, verifiziert); isolierte Fehlerquelle |
| 3 | Nur Kamera-Patch | ebenfalls 1 Stelle, verifiziert; unabhängig von Drucker-Netzwerk |
| 4 | Drucker: nur lageunabhängige, risikoarme Stellen (NOP, Datenpatches, 3 eingegrenzte) | kein Payload, keine Cross-Function-Neukodierung; geringstes Risiko |
| 5 | Payload platzieren, dann Payload-abhängige Stellen | Payload ist Voraussetzung für Gruppe A/B und Gruppe-2-DIFF |
| 6 | DIFF-Blöcke | erst nach After-Neukodierung sinnvoll |
| 7 | Gesamtpatch | Zusammenspiel aller Teile |
| 8 | Stabilitätstest | Dauerverhalten, erst wenn Funktion steht |

Keine Stelle gilt als „getestet“, bevor ihre konkreten Voraussetzungen erfüllt sind.

## Abhängigkeiten des Drucker-Tests

- **GUI** und **Kamera** sind eigene Binaries; ihre Patches sind verifiziert, aber
  ebenfalls hardware-ungetestet. Der Gesamttest (Schritt 7) setzt sie als bestanden voraus.
- **`daemon-000`** (Dual Trust) und **`libcolib.so`** sind byte-identisch; ihre
  Patches übertragen unverändert. Der Drucker-Boot (Dual Trust / Reactor) hängt an
  ihnen — bei Boot-Problemen zuerst dort prüfen.
- **Bootloader** (`uboot`/`boot0`) identisch; dennoch Teil des Pakets (Brick-Risiko bei
  fehlerhaftem Flash) → Rollback-Weg zwingend.

## Nur-nach-Emulation-Tests

Diese Hardware-Fälle sind erst nach bestandener Emulation sinnvoll: H3 (Upload/Cancel,
→ E3/E5), H5 (LAN/WAN-MQTT, → E7), H6 (Gerätestatus, → E4), sowie alle DIFF- und
Payload-abhängigen Funktionen (→ E10/E7/E8).

## Übersicht

- Teststufen: 5 (0–4).
- Kritische Gates: Payload-Basis final, After-Bytes verifiziert, Emulation bestanden.
- Reihenfolge: Referenz → GUI → Kamera → lageunabhängig → Payload → DIFF → Gesamt → Stabilität.
- Vor Payload-Platzierung testbar: Referenz, GUI, Kamera, lageunabhängige Druckerstellen.
- Erst nach Payload: Gruppe-A-/B-Referenzen, Gruppe-2-DIFF.
- Erst nach DIFF-After-Neukodierung: die 6 DIFF-Blöcke, Gesamtpatch.
