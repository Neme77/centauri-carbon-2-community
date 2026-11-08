# Offizielles ELEGOO-Paket vom eigenen Drucker sichern (privat)

> **Ergebnis vom 2026-11-08 [belegt]:** Gesichert wurde `cc2_eeb001_02.01.01.13_20261020214142.zip.sig`. Der erste Versuch mit `ota_grabber.sh` scheiterte, weil der Drucker etwa eine Minute nach dem Download neu startete, bevor die Kopie auf den Stick fertig war. Funktioniert hat `ota_keeper.sh`: Hardlink nach `/opt/usr/ota_keep/`, dann nach erneutem V4.4.3-Flash `sh /mnt/exUDISK/ota_keeper.sh export`. Hinweise: Auf dem Drucker fehlen `nohup`, `setsid`, `cksum` und `stat`. Das Paket liegt als `/opt/usr/firmware/elegoo_firmware`. Auf V4.4.3 ist der SSH-Host-Key nach jedem Flash neu (`ssh-keygen -R`).

> **Nur lokal.** Diese Anleitung enthält die private Drucker-IP `192.168.2.220`.
> Vor jeder Veröffentlichung durch `PRINTER-IP` ersetzen und ins Englische übersetzen.
> Alle Schritte führst **du** am Drucker aus. Claude baut keine Verbindung zum Drucker auf.

Kennzeichnung:
- **[belegt]** = im Repo oder per Messung nachgewiesen.
- **[Vermutung]** = nicht geprüft.

## Ausgangslage

- **[belegt]** Das Repo enthält kein `fwdl.sh` und keinen Code, der beschreibt, wohin die Firmware OTA-Updates lädt. Der Download läuft im Vendor-Programm (`daemon-000` bzw. `elegoo_printer`), daher ist **kein Cache-Pfad bekannt**.
- **[gemessen, aber nicht aussagekräftig]** Port-Check vom 2026-11-08 an 192.168.2.220. Der Drucker wurde ungefähr zu dieser Zeit auf V4.4.3 gedowngradet; welcher Firmware-Stand geantwortet hat, ist unklar. Ergebnis:

  | Port | Ergebnis |
  |---|---|
  | 22 (SSH) | abgelehnt |
  | 8081 | abgelehnt |
  | 7125 | abgelehnt |
  | 80 | offen, liefert 404 |
  | 1883 | offen |
  | 8080 | offen |

  Dass Stock-Firmware SSH schließt, ist nur durch den Nutzerbericht #309 (auf 02.01.01.12) belegt. Auf V4.4.3 sollte Port 22 offen sein; das bitte nach dem Neustart prüfen.
- **[Vermutung]** Auf 02.01.01.13 bietet „Auf Updates prüfen“ nichts an, weil die Version schon aktuell ist.

**Gewählter Weg:** Zuerst Community-Firmware V4.4.3 per USB flashen; dann ist SSH offen. Danach das Online-Update auf 02.01.01.13 anstoßen und die Datei abgreifen, bevor sie installiert oder gelöscht wird. Parallel dazu als Rückfallebene den Netzwerkverkehr mitschneiden.

## Risiken vor dem Downgrade

- **[belegt, Nutzerbericht #309]** Der Weg 02.01.01.12 → V4.4.3 per Offline-Update hat funktioniert. **[Vermutung]** Ob das auch von 02.01.01.13 aus geht, ist unbekannt.
- **[belegt]** Das Paket enthält `uboot` und `boot0` (`builder/current/core/firmware_builder.py`, `CPIO_FILES`). Der Bootloader wird also ersetzt.
- Lehnt der Drucker das Paket ab, hörst du auf und notierst die Meldung. Versionsdaten nicht ändern, Prüfungen nicht umgehen (`docs/INSTALL.md`).
- Datensicherung: Laut #309 überlebt `/opt/usr` das Flashen, aber SSH-Host-Key und `authorized_keys` sind danach neu bzw. weg.

## Schritt 1 – V4.4.3 per USB flashen

1. Lade die V4.4.3-Firmware **nur** aus den GitHub-Releases von `Neme77/centauri-carbon-2-community` (Tag `V4.4.3`) und vergleiche die SHA-256 mit dem Wert in den Release-Notes:
   ```bash
   shasum -a 256 ~/Downloads/CC2_V4_4_3_*.zip.sig
   ```
2. Folge `docs/FIRST_RUN.md`: die Datei auf den USB-Stick kopieren, dann am Drucker **Einstellungen → Auf Updates prüfen → Offline-Update**.
3. Nach dem Neustart prüfst du SSH. Das Standard-Passwort steht in `docs/INSTALL.md`, Abschnitt „SSH access reference“.
   ```bash
   ssh root@192.168.2.220
   ```
4. **Noch nicht „Auf Updates prüfen“ antippen.** Erst Schritt 2 vorbereiten.

## Schritt 2 – Beobachtung starten (auf dem Drucker, nur lesend)

**Empfohlen: das Grabber-Skript** `builder/current/tools/capture/ota_grabber.sh`. Es liegt als `ota_grabber.sh` im Hauptverzeichnis des USB-Sticks und erledigt die Schritte 2 und 3 automatisch:
- Es beobachtet `/opt/usr`, `/tmp`, `/var/tmp` und übliche Update-Ordner.
- Es kopiert jede neue Datei über 10 MB, sobald ihre Größe stabil bleibt, nach `/mnt/exUDISK/ota_grabbed/`, prüft die Prüfsumme und protokolliert alles in `/mnt/exUDISK/ota_grabber_log.txt`.

```sh
grep exUDISK /proc/mounts
nohup sh /mnt/exUDISK/ota_grabber.sh >/dev/null 2>&1 &
tail -f /mnt/exUDISK/ota_grabber_log.txt
```

Erst wenn im Log „Grabber started“ steht, am Display „Auf Updates prüfen“ antippen.

- Meldet `grep` einen anderen Mount-Punkt, z.B. `/mnt/exUDISK/sda1`, starte das Skript mit `USB_ROOT=<dieser Pfad> sh <dieser Pfad>/ota_grabber.sh`.
- Beenden: `kill "$(cat /mnt/exUDISK/ota_grabbed/.pid)"`.

Die manuelle Variante unten bleibt als Kontrolle.

In einem ersten Terminal per SSH auf dem Drucker:

```sh
df -h; mount | grep -vE 'proc|sys|cgroup'
```

Damit siehst du die beschreibbaren Bereiche. Typische Kandidaten sind `/tmp`, `/opt/usr`, `/mnt/exUDISK`, `/userdata`, `/data` und `/update`; das ist **[Vermutung]**.

Marker setzen, damit nur neue Dateien auftauchen:

```sh
touch /tmp/.capture-marker
```

Beobachtungs-Schleife: Sie zeigt alle 3 Sekunden neue Dateien über 10 MB. Abbrechen mit Strg+C.

```sh
while true; do date; for d in /tmp /opt/usr /mnt /userdata /data /update /opt; do [ -d "$d" ] && find "$d" -xdev -type f -newer /tmp/.capture-marker -size +10000k 2>/dev/null -exec ls -l {} \; ; done; sleep 3; done
```

Falls `find` auf dem Drucker `-newer` oder `-size` nicht kennt (BusyBox-Variante, **[Vermutung]**), nimm stattdessen:

```sh
while true; do date; ls -lt /tmp /opt/usr 2>/dev/null | head -20; sleep 3; done
```

## Schritt 3 – Update anstoßen und Datei abgreifen

1. Optional, parallel: Netzwerk-Mitschnitt starten (Schritt 4).
2. Am Display **Einstellungen → Auf Updates prüfen** antippen. Wird 02.01.01.13 angeboten, den Download starten.
3. Sobald in der Schleife eine große neue Datei erscheint: warten, bis ihre Größe zwischen zwei Durchläufen gleich bleibt. Dann sofort in einem **zweiten Terminal am Mac** kopieren, mit Pfad und Dateinamen aus der Schleife:
   ```bash
   mkdir -p ~/CC2-capture && cd ~/CC2-capture
   ```
   ```bash
   scp -O root@192.168.2.220:/PFAD/AUS/DER/SCHLEIFE ./
   ```
   Falls `scp` scheitert:
   ```bash
   ssh root@192.168.2.220 'cat /PFAD/AUS/DER/SCHLEIFE' > ./elegoo-ota.bin
   ```
4. Prüfsumme bilden, zweimal kopieren und vergleichen, damit klar ist, dass die Datei nicht mehr wuchs:
   ```bash
   shasum -a 256 ~/CC2-capture/*
   ```
5. **[Vermutung]** Je nach Firmware installiert der Drucker sofort nach dem Download. Ob dir dafür genug Zeit bleibt, ist offen. Klappt es nicht, kannst du Schritt 1 bis 3 wiederholen.

Hinweis: Die gespeicherte Datei kann ein anderes Format haben als das USB-Paket `….zip.sig`, z.B. eine bereits ausgepackte `.swu`. Das zeigt dann `inspect_base.py`.

## Schritt 4 – Rückfallebene: Netzwerk-Mitschnitt (nur Analyse)

Ziel ist nur, die Download-Quelle (Hostname, eventuell URL) zu sehen. Keine automatisierten Massen-Downloads.

- **Router- oder DNS-Log:** Zeigt, welche Hostnamen der Drucker während des Updates auflöst. Möglich sind z.B. das DNS-Log eines Pi-hole oder AdGuard Home oder ein Router mit Paketmitschnitt (bei der Fritz!Box `http://fritz.box/html/capture.html`).
- **Wireshark/tcpdump:** Der Mac sieht den Verkehr des Druckers nur über einen Mirror-Port am Switch oder den Mitschnitt des Routers. Filter: `host 192.168.2.220`. Bei HTTPS siehst du nur den Hostnamen (TLS-SNI), nicht den Pfad **[Vermutung]**; bei HTTP auch die volle URL.
- Eine gefundene URL nur öffnen, wenn sie eindeutig zu ELEGOO gehört. Die Datei danach genauso mit `shasum` und `inspect_base.py` prüfen.

**Alternative:** den ELEGOO-Support nach dem offiziellen 02.01.01.13-Paket für das Offline-Update fragen.

## Schritt 5 – Datei an den Builder übergeben (am Mac)

```bash
cp ~/CC2-capture/<datei> "builder/current/original_firmware/"
```

```bash
shasum -a 256 builder/current/original_firmware/*
```

```bash
python3 -I builder/current/tools/inspect_base.py builder/current/original_firmware/<datei> --aes-key builder/current/keys/cc2_aes_key_v1.bin --out builder/current/analysis
```

`builder/current/original_firmware/` und `builder/current/analysis/` sind von Git ausgeschlossen. Danach die Werte aus `builder/current/analysis/report.json` (`profile_values`) in `builder/current/base_profiles/02.01.01.13.json` eintragen; siehe `UPDATE_PLAN_02.01.01.13.md`, Abschnitt 5.
