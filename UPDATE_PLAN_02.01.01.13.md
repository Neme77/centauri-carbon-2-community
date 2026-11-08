# Update-Plan: Elegoo-Basis 02.01.01.13

**Status:** private Vorbereitung auf dem lokalen Branch `update-2.03.03.13`. Nicht veröffentlichen.
**Basis:** `develop` @ `47efbac0` (Merge von #321).
**Ziel:** Der Community-Build soll auf der neuesten offiziellen Elegoo-Firmware aufbauen, damit beim Flashen kein Downgrade entsteht.

## Korrektur: Die Zielversion ist 02.01.01.13, nicht 02.03.03.13

**[belegt, 2026-11-08]** Das Online-Update deines Druckers hat `cc2_eeb001_02.01.01.13_20261020214142.zip.sig` geladen. `opt/inst/firmware_version/versions.json` im neuen RootFS meldet `ota_version` = `02.01.01.13`. Die Zeichenfolge „02.03.03.13“ kommt im RootFS nicht vor. Wo im Text unten noch „02.03.03.13“ steht, ist 02.01.01.13 gemeint. Der Branch-Name `update-2.03.03.13` bleibt vorerst unverändert.

## Ergebnis der Analyse des Original-Pakets (belegt, offline am Mac gemessen)

So wurde das Paket gesichert:
- Online-Update auf V4.4.3, dabei Hardlink mit `builder/current/tools/capture/ota_keeper.sh`.
- Danach `ota_keeper.sh export` auf den USB-Stick; jede Kopie wurde auf dem Drucker per `sha256sum` bestätigt.
- Lokal liegen die Dateien in `builder/current/original_firmware/`, die Analyse in `builder/current/analysis/02.01.01.13/`. Beides ist von Git ausgeschlossen.

| Prüfung | Ergebnis |
|---|---|
| Eingebettete SHA-256 im äußeren Paket (0x04) und in der inneren `.swu.sig` (0x80) | passt |
| RSA-Signatur äußeres Paket, Manifest (0x83), innere SWU mit `keys/cc2_stock_public.pem` | alle **gültig**, kein Schlüsselwechsel |
| ZIP-Inhalt | `ota-package-list.json.sig` (720 B) + eine `.swu.sig`; Einträge **deflated** (der Builder schreibt heute `ZIP_STORED`) |
| `.swu.sig` im ZIP vs. gesicherte Datei | byte-gleich |
| Entschlüsselte `.swu` | Länge = `data_size` im Kopf, CPIO newc/crc, gleiche 7 Einträge in gleicher Reihenfolge, `cpio_item_md5` stimmt |
| RootFS | SquashFS xz, Blockgröße 262144 (wie bisher) |
| Kernel | `Linux version 5.4.61 … #50 SMP PREEMPT Tue Oct 20` (neu gebaut, gleiche Hauptversion) |
| Manifest-Inhalt (`version`, `update_class`) | nicht lesbar ohne AES-Eingabe → bleibt Platzhalter |

Pins im Profil `builder/current/base_profiles/02.01.01.13.json` im Vergleich mit 02.01.01.07:

| Pin | SHA-256 | vs. 02.01.01.07 |
|---|---|---|
| `stock_outer` | `538b0ea40b7a5cae…` | neu |
| `stock_inner` | `06fd9fc7b9c02857…` | neu |
| `stock_swu` | `dcff98bc2e6513c6…` | neu |
| `sw-description` | `6598586ada5e7e92…` | **gleich** |
| `resource` | `aacfe9a9d847f4be…` | **gleich** |
| `uboot` | `769d55b96ed4dff4…` | **gleich** |
| `boot0` | `b18b3f31b3d378b5…` | **gleich** |
| `kernel` | `bf9659432181d534…` | neu |
| `stock_rootfs` | `30b63303eb13175e…` | neu |
| `stock_gui` | `8d6cac2aecb26b0e…` | neu |
| `stock_printer` | `f0111e0b52c2ea39…` | neu |
| `daemon_stock` | `6fed6657e814f010…` | **gleich** |
| `stock_ai_camera` | `e8df8e57fdc1866b…` | neu |
| `libcolib` | `14288a4a73c339b0…` | **gleich** |

**Patch-Stellen** (`inspect_base.py`-Logik, Details in `analysis/02.01.01.13/patch_sites.json`):

| Patch | Ergebnis |
|---|---|
| Dual Trust (`daemon-000`) | unverändert an der qualifizierten Stelle; das Programm ist byte-gleich |
| HTTP/Upload (`elegoo_printer`, 11 Stellen) | 6 eindeutig wiedergefunden, mehrere mit gleichem Versatz +0x7790; 3 mehrdeutig, 2 fehlen |
| Netzwerk (`elegoo_printer`, 18 Stellen) | 3 eindeutig, 9 mehrdeutig, 6 fehlen. Ein Teil davon ist erwartbar, weil die Vorher-Bytes vom HTTP-gepatchten Zwischenstand stammen. |
| Z-Offset (`ec-eeb001-gui`) | nicht gefunden → neu ermitteln |
| Kamera (`ai_camera`) | nicht gefunden → neu ermitteln |

**Folgen [Vermutung, außer wo belegt]:**
- Das Bootloader-Risiko beim Wechsel zwischen den beiden Basen entfällt: `uboot`/`boot0` sind identisch [belegt].
- Dual Trust und Reactor-libco können unverändert bleiben [belegt: gleiche Hashes].
- Zu portieren bleiben die Patches in GUI, `elegoo_printer` und `ai_camera`, die Reactor-Bindings für das neue `elegoo_printer` und eventuell die Kernelmodule (gleiche Version 5.4.61, aber neuer Build; ob sie geladen werden, ist ungeprüft).
- Abschnitt 5 (Platzhalter) ist überholt: offen sind nur noch `manifest.version`, `manifest.update_class` und `ported_patch_set`.

Kennzeichnung im ganzen Dokument:
- **[belegt]** = im Code bzw. Repo gefunden, mit Datei:Zeile, oder selbst gemessen.
- **[Vermutung]** = nicht geprüft.

---

## 0. Kernaussage

**[belegt]** Eine neue Versionsnummer reicht nicht. Der Builder ist absichtlich fail-closed und an die exakten Bytes von 02.01.01.07 gebunden:

- `builder/current/core/firmware_builder.py:27-61`: SHA256-Pins für jede Container-Stufe des Original-Pakets, für die CPIO-Einträge und für drei Vendor-Programme. Dazu die Dual-Trust-Offsets `HOOK_OFFSET=0x2C68C`, `INJECT_OFFSET=0x852AC` und die ELF-Segmentgrößen.
- `firmware_builder.py:538-574`: Pins für `ec-eeb001-gui`, `elegoo_printer` (`8039081f…`), `daemon-000` und den Kamera-Patch.
- `experimental/firmware-port/*44-manifest.json`: Binärpatches mit virtueller Adresse (VA), Offset und Vorher-/Nachher-Bytes. Laut `experimental/firmware-port/README.md:3` gelten sie „only to official CC2 OTA 02.01.01.07“.
- `builder/current/core/connections_component.py:17-22, 93-94`: Kernelmodule (`*.ko`) für `lib/modules/5.4.61`.
- `cc2-control/installer/compatibility.json:3-5`: CC2 Control verweigert die Installation bei anderem `elegoo_printer`- oder `daemon-000`-Hash.

Folge: Ein Build auf 02.03.03.13 braucht eine **Neu-Portierung aller Binärpatches**. Dazu gehört Reverse Engineering der neuen Programme, erst offline mit Emulationstests, dann mit Hardware-Tests. Dieser Branch liefert nur die Vorbereitung:
1. Die Basisversion steht zentral in `builder/current/BASE_FIRMWARE`; pro Version gibt es ein Profil in `builder/current/base_profiles/`.
2. Ein vollständiges 02.03.03.13-Profil mit Platzhaltern. Der Builder bricht sicher ab, solange Platzhalter offen sind und der Patch-Satz nicht portiert ist.
3. `builder/current/tools/inspect_base.py`: liest das neue Paket offline ein und zeigt, welche alten Patch-Stellen noch passen.
4. Eine Anleitung zum Sichern des Original-Pakets: `docs/CAPTURE_VENDOR_PACKAGE.de.md`.

## 1. Projektregeln (Zusammenfassung)

| Thema | Regel | Quelle |
|---|---|---|
| Branches | `develop` ist Standard, `main` nur für Releases; Arbeits-Branches heißen `feature/…` und `fix/…` | Branch-Liste, `RELEASING.md` (Tagging) |
| Commits | englischer Imperativ, Satzanfang groß, ohne Präfix | `git log` |
| PRs | eine logische Änderung pro PR; Regressionstest wo möglich; Doku bei Verhaltensänderung | `CONTRIBUTING.md:5-11` |
| Vendor-Material | nie committen; getrennt halten; Herkunft und Prüfsumme notieren | `CONTRIBUTING.md:15-17`, `.gitignore:5-18` |
| Doku | nutzerseitig auf Englisch | `CONTRIBUTING.md:18` |
| Tests | `cd cc2-control && make clean test CROSS= CC=gcc`; `python3 -m unittest discover -s builder/current/tests -p 'test_*.py'` | `CONTRIBUTING.md:22-26`, `RELEASING.md:22` |
| Build | Windows mit WSL Ubuntu, GCC 6.5.0/glibc 2.23 ARM, SquashFS 4.6.1, openssl, cpio | `docs/BUILD.md`, `builder/current/README.md:54-62` |
| Lizenz | GPL-3.0-only nur für eigene Beiträge; **nicht** für ELEGOO-Images, gepatchte Vendor-Programme, Marken oder Signierschlüssel | `LICENSE`, `NOTICE.md:8-12` |
| Versionsfelder | „do not change version metadata or bypass its checks“ | `docs/INSTALL.md:23` |

**[belegt]** Code-of-Conduct-Datei und Issue-/PR-Vorlagen gibt es im Repo nicht (`.github/` enthält nur `scripts/` und `workflows/`).

**Bestehende Arbeit zu 02.03.x [belegt, GitHub-API am 2026-11-08]:** Die Suche nach „2.03“, „02.03“ und „2.03.03.13“ in Issues/PRs ergab 0 Treffer, auch kein Branch und kein Release. Verwandt ist Issue #309: Das Online-Update hat 02.01.01.12 über V4.4.3 installiert. Danach wurde V4.4.3 (Manifest-Version 02.01.01.07) per Offline-Update wieder angenommen, also ohne Downgrade-Sperre. Das ist ein Nutzerbericht, kein Code.

## 2. Analyse

Die Fundstellen-Tabellen stehen im Anhang A (Versions-Strings) und Anhang B (SHA256-Pins und Offsets).

## 3. Plan

### a) Anzupassende Dateien

**In diesem Branch (Gerüst):**
- `builder/current/BASE_FIRMWARE` (neu) und `builder/current/base_profiles/{02.01.01.07,02.03.03.13}.json` (neu).
- `builder/current/core/firmware_builder.py`: Version, Paketname, SWU-Namen und Manifest-Version kommen aus dem Profil. Der Builder bricht ab bei offenen Platzhaltern oder nicht portiertem Patch-Satz.
- `builder/current/launch_helpers/run_build.py`: Paketpfad aus dem Profil; `BUILDER_SHA256` neu berechnet.
- `builder/current/prepare_vendor.py`: Paketpfad aus dem qualifizierten Profil.
- `builder/current/Build-Release.ps1`: Paketpfad aus dem Profil.
- Tests: `builder/current/tests/test_44_port_static.py`, `builder/current/launch_helpers/tests/test_launcher.py`, neu `builder/current/tests/test_base_profiles.py`.
- `builder/current/tools/inspect_base.py` (neu), `.gitignore`, `docs/CAPTURE_VENDOR_PACKAGE.de.md` (neu).

**Später, für die echte Portierung (braucht das 02.03.03.13-Paket):**
- Neuer Patch-Satz, z.B. `experimental/firmware-port-0203/`: Z-Offset (GUI), HTTP-Upload und LAN/WAN (`elegoo_printer`), Kamera (`ai_camera`), Dual Trust (`daemon-000`) und Reactor-Bindings (`experimental/firmware-port/reactor/port-manifest.json`), jeweils mit Emulationstests nach dem Vorbild von `experimental/firmware-port/test_*44_emulation.py`.
- `prepare_reactor.py` (Bindings und libco aus der neuen Basis), `core/native_display_component.py` (GUI-Hash), `core/camera_component.py`, `core/connections_component.py` (Module für den Kernel der neuen Basis).
- `cc2-control/installer/compatibility.json` und eventuell `cc2-control/FIRMWARE_VERSION`, also eine neue Kompatibilitätsfamilie, z.B. „4.5“. Das ist mit dem Maintainer zu klären.
- `cc2-control/src/canvas_unload.h:30`: Konstante aus `elegoo_printer` prüfen. `cc2-control/src/external_filament_catalog.h:1`: Daten aus der Stock-GUI prüfen.
- Doku: README, INSTALL, FIRST_RUN, UPDATE_CC2, BUILD, `builder/current/README.md`.

### b) Injektionspunkte und mögliche Änderungen durch die neue Basis

**[belegt]** Injektionspunkte, `firmware_builder.py:538-589`:

| Funktion | Ziel im RootFS | Quelle |
|---|---|---|
| SSH/root | `usr/sbin/sshd`, `etc/init.d/sshd`, `etc/rc.d/S50sshd`, `etc/rc.d/K50sshd`, `etc/ssh/sshd_config`, Host-Keys | `firmware_builder.py:553-563`, `components/ssh/` |
| Z-Offset-Menü | Patch in `opt/bin/ec-eeb001-gui` (VA `0x752d0` → `0x756d8`) | `firmware_builder.py:546`, `experimental/firmware-port/patch_zoffset44.py` |
| HTTP-Upload, LAN/WAN-MQTT | Patch in `opt/bin/elegoo_printer` | `firmware_builder.py:441-450`, `http44-manifest.json`, `network44-manifest.json` |
| Kamera-Koexistenz | Patch in `opt/bin/ai_camera` | `firmware_builder.py:548-551`, `camera44-manifest.json` |
| Dual Trust (Community-Signatur) | `opt/inst/daemon-000/daemon-000`, ELF-PT_LOAD-Umbau | `firmware_builder.py:59-61`, `dual44-manifest.json`, `experimental/firmware-port/README.md:33-38` |
| CC2 Control, Reactor, Kamera-Runtime | `/opt/...`, Init-Skripte aus `components/*/` | `firmware_builder.py:564-575` |
| Connections und USB-Ethernet | `/lib/modules/5.4.61/cc2`, `/opt/inst/cc2-connections` | `builder/current/README.md:36-41` |
| Display-Ressourcen | `sw-description` bekommt `ota_flag` | `core/resource_update.py:11-22` |

**Klipper [belegt]:** Es gibt keine separat gepatchte Klipper-Installation. Die Klipper-artige Logik (z.B. `idle_timeout`, `EXCLUDE_OBJECT`) läuft im Vendor-Programm `elegoo_printer` (`docs/CC2_CONTROL.md:41`, `:123`). Klipper-Anpassungen sind also die Patches in `elegoo_printer` plus CC2 Control über MQTT/UDS.

**Mögliche Änderungen durch 02.03.03.13 [Vermutung]:**
- Alle fünf gepatchten Programme sind neu kompiliert. Damit verschieben sich alle Offsets, VAs und Funktionsadressen; Reactor-Bindings und Strukturlayouts ändern sich.
- Neuer Kernel: Die Module für `5.4.61` laden dann nicht, und `connections_component.py` würde abbrechen.
- Geänderte Init-Skripte (`etc/init.d/printer`, `opt/bin/run_printer.sh`) oder eine andere Startreihenfolge, die `S50sshd` und die CC2-Control-Init betreffen.
- Andere Partitionen oder Slots in `sw-description` (A/B), zusätzliche CPIO-Einträge.
- Geänderter Updater in `daemon-000`, z.B. mit Versionsprüfung oder anderem Schlüssel. Davon hängt Dual Trust ab.

### c) Fallstricke beim Entpacken des Pakets

**[belegt] Heutige Kette (02.01.01.07),** `firmware_builder.py:21-25, 205-262, 581`:
1. Außen: ELEG-Header `0x200` Bytes, Typ `0x04`, Version 1, Subtype 2, im Klartext, RSA-2048/SHA256-signiert (`keys/cc2_stock_public.pem`).
2. Darin ein ZIP mit `ota-package-list.json.sig` (ELEG `0x83`, AES-CBC, PKCS#7) und genau einer `*.swu.sig` (ELEG `0x80`, AES).
3. SWU = CPIO newc/crc mit `sw-description, resource, uboot, boot0, kernel, rootfs, cpio_item_md5`.
4. `rootfs` = SquashFS (Rebuild mit `xz`, Blockgröße 262144, `-no-tailends`, `-all-root`).

**Risiken [Vermutung]:**
- AES-Schlüssel oder Stock-RSA-Schlüssel rotiert. Dann scheitern `verify_signature` bzw. das Entschlüsseln. `inspect_base.py` meldet das getrennt.
- Neue ELEG-Version oder neuer Subtype. Heute erwartet `firmware_builder.py:217` genau Version 1 und Subtype 2.
- Mehrere `.swu.sig` im ZIP (`:242` erwartet genau eine) oder andere Dateinamen im ZIP.
- Andere oder zusätzliche CPIO-Einträge, andere `cpio_item_md5`-Struktur, andere Modi/Metadaten (`:605-608`).
- SquashFS mit anderem Kompressor oder anderer Blockgröße. Partitionsgrößen-Limit: Das neue RootFS ist eventuell größer, und Community-Komponenten passen nicht mehr in die Partition.
- Das Feld `"version"` im Manifest (`:623`) und `update_class` haben eventuell neue Bedeutung.

### d) Risiken für den Drucker beim Flashen

- **Downgrade-Prüfung:** **[belegt]** Der Builder schreibt heute fest `"version":"02.01.01.07"` ins Manifest (`firmware_builder.py:623`). **[belegt, Nutzerbericht #309]** Auf 02.01.01.12 wurde das nicht blockiert. **[Vermutung]** Für 02.03.03.13 ist das unbekannt. Nach `docs/INSTALL.md:23` dürfen Versionsfelder nicht manipuliert werden, um Prüfungen zu umgehen. Auf diesem Branch gibt es deshalb nur ehrliche Werte aus dem echten Paket.
- **Bootloader:** **[belegt]** Das Paket enthält `uboot` und `boot0` (`:24`). **[Vermutung]** Ein Paket auf alter Basis schreibt den alten Bootloader zurück. Das ist das größte Brick-Risiko, falls 02.03.03.13 Bootloader- oder Partitionsänderungen mitbringt.
- **Signatur:** **[Vermutung]** Akzeptiert 02.03.03.13 den bisherigen Stock-Schlüssel nicht mehr, lehnt der Drucker das Paket ab. Das ist sicher. Wird das Paket angenommen, der Dual-Trust-Patch passt aber nicht, sind spätere Community-signierte Updates nicht mehr möglich.
- **Rollback/A-B:** **[Vermutung]** Die Slot-Logik ist nicht dokumentiert. Ein fehlerhaftes RootFS kann im aktiven Slot landen. Recovery wäre dann nur über das USB-Offline-Update möglich, falls die GUI startet.
- **Kernelmodule:** **[Vermutung]** Bei neuem Kernel laden die Module nicht. Das ist harmlos, solange Ethernet deaktiviert bleibt; der Builder bricht aber vorher ab.
- **Allgemein:** Kein Flashen ohne vollständige Emulationstests, `--require-golden`-äquivalente Prüfungen und die Hardware-Checkliste aus `docs/TESTING.md`.

### e) Lizenz und spätere Veröffentlichung

- Eigene Skripte und Doku stehen unter GPL-3.0-only (`NOTICE.md:8`).
- **Keine** ELEGOO-Pakete, keine entpackten Inhalte, keine gepatchten Vendor-Programme und keine Schlüssel ins Repo (`NOTICE.md:10`, `CONTRIBUTING.md:15`). Hashes, Offsets und Vorher-/Nachher-Byte-Folgen veröffentlicht das Projekt bereits heute in Manifesten. Ob das für 02.03 gewünscht ist, entscheidet der Maintainer.
- Fertige Firmware-Images enthalten Vendor- und Drittkomponenten (z.B. OpenSSH). Laut `NOTICE.md:12` fehlt noch ein vollständiges Lizenz-/Quellen-Inventar, und die GPL-Quellpflichten sind offen.
- Die Drucker-IP aus der lokalen Anleitung vor jeder Veröffentlichung durch `PRINTER-IP` ersetzen.
- Branch vor einem PR umbenennen (z.B. `feature/base-02.03.03.13`) und auf `develop` zielen. Doku auf Englisch übersetzen.

**Fragen an Neme77:**
1. Ist eine Unterstützung von 02.03.x gewünscht, oder bleibt 02.01.01.07 die einzige qualifizierte Basis?
2. Neue Kompatibilitätsfamilie (`cc2-control/FIRMWARE_VERSION`, z.B. 4.5) oder Erweiterung von 4.4?
3. Ist der Profil-Mechanismus (`BASE_FIRMWARE` plus `base_profiles/`) als Struktur akzeptabel, oder lieber ein eigener Builder-Ordner pro Basis (wie `builder/v4.1-r8`, `builder/v4.2`)?
4. Wie läuft die Hardware-Qualifizierung, und wer testet?
5. Umgang mit den Stock-Signierschlüsseln und dem AES-Schlüssel bei einer neuen Basis: Hat der Maintainer Erkenntnisse über eine Rotation?
6. Ist Dual Trust im neuen `daemon-000` weiterhin der gewünschte Ansatz?
7. Wie soll mit dem Online-Update der Vendor-Firmware umgegangen werden (#309)?

## 4. Gesichertes Paket: welche Datei wohin

1. Paket als `builder/current/original_firmware/<echter Dateiname>.zip.sig` ablegen. Der Ordner ist per `.gitignore` (`**/original_firmware/`) ausgeschlossen.
2. `shasum -a 256 builder/current/original_firmware/*.zip.sig` (unter Linux `sha256sum`).
3. `python3 -I builder/current/tools/inspect_base.py builder/current/original_firmware/<datei>.zip.sig --aes-key builder/current/keys/cc2_aes_key_v1.bin --out builder/current/analysis`.
4. Die Werte aus `builder/current/analysis/report.json` in `builder/current/base_profiles/02.03.03.13.json` eintragen; die Platzhalter-Liste steht in Abschnitt 5.

## 5. Platzhalter in `builder/current/base_profiles/02.03.03.13.json`

| Platzhalter | gemeinte Originaldatei | Quelle des Werts |
|---|---|---|
| `TODO_FILENAME_STOCK_PACKAGE` | Dateiname des offiziellen Pakets `….zip.sig` | Dateiname der gesicherten Datei |
| `TODO_SHA256_STOCK_OUTER` | das ganze `….zip.sig` | `shasum -a 256` bzw. `report.json: outer.sha256` |
| `TODO_SHA256_STOCK_INNER` | `*.swu.sig` im ZIP | `report.json: inner.sha256` |
| `TODO_SHA256_STOCK_SWU` | entschlüsselte SWU (CPIO) | `report.json: swu.sha256` |
| `TODO_SHA256_SW_DESCRIPTION` | CPIO `sw-description` | `report.json: cpio.sw-description` |
| `TODO_SHA256_RESOURCE` | CPIO `resource` | `report.json: cpio.resource` |
| `TODO_SHA256_UBOOT` | CPIO `uboot` | `report.json: cpio.uboot` |
| `TODO_SHA256_BOOT0` | CPIO `boot0` | `report.json: cpio.boot0` |
| `TODO_SHA256_KERNEL` | CPIO `kernel` | `report.json: cpio.kernel` |
| `TODO_SHA256_ROOTFS` | CPIO `rootfs` (SquashFS) | `report.json: cpio.rootfs` |
| `TODO_SHA256_EC_EEB001_GUI` | RootFS `opt/bin/ec-eeb001-gui` | `report.json: rootfs_files` (braucht unsquashfs) |
| `TODO_SHA256_ELEGOO_PRINTER` | RootFS `opt/bin/elegoo_printer` | dito |
| `TODO_SHA256_DAEMON_000` | RootFS `opt/inst/daemon-000/daemon-000` | dito |
| `TODO_SHA256_AI_CAMERA` | RootFS `opt/bin/ai_camera` | dito |
| `TODO_SHA256_LIBCOLIB_SO` | RootFS `opt/lib/libcolib.so` | dito |
| `TODO_VERSION_EMBEDDED_FILENAME` | Teilstring im Dateinamen des äußeren ELEG-Headers | `report.json: outer.embedded_filename` |
| `TODO_MANIFEST_VERSION` / `TODO_MANIFEST_UPDATE_CLASS` | Felder in `ota-package-list.json` | `report.json: manifest` |
| `ported_patch_set: false` | – | wird erst mit einem portierten und getesteten Patch-Satz `true` |

`TODO_SHA256_*_PATCHED` und `TODO_OFFSET_*` gibt es bewusst **nicht** im Profil. Sie entstehen erst bei der Portierung in einem eigenen Patch-Satz und lassen sich nicht aus einem `shasum` ableiten.

## 6. Original-Paket vom eigenen Drucker sichern

Siehe `docs/CAPTURE_VENDOR_PACKAGE.de.md`.

**[belegt]** Im Repo gibt es kein `fwdl.sh` und keinen Code, der den OTA-Download-Cache beschreibt. Der Download läuft im Vendor-Programm, ein Cache-Pfad ist also nicht belegt.

**[gemessen, nicht aussagekräftig: Port-Check 2026-11-08 an 192.168.2.220 lief vermutlich während des Downgrades auf V4.4.3, TCP-Connect ohne Login]:**

| Port | Ergebnis |
|---|---|
| 22 | abgelehnt (code 61) |
| 80 | offen, `GET /` → 404 |
| 1883 | offen |
| 8080 | offen |
| 8081 | abgelehnt |
| 7125 | abgelehnt |

Dass Stock-Firmware SSH schließt, ist nur durch #309 belegt. Zum Sichern dient `builder/current/tools/capture/ota_grabber.sh`, das du per SSH auf V4.4.3 startest; Schritte in `docs/CAPTURE_VENDOR_PACKAGE.de.md`.

## 7. Prüfergebnis (macOS, 2026-11-08)

- `bash -n`: 54 Shell-Skripte, 0 Fehler. **shellcheck** ist nicht installiert und wurde nicht ausgeführt.
- `python3 -m py_compile`: 214 Python-Dateien, 0 Fehler.
- Builder-Suite (`unittest discover -s builder/current/tests`): 47 Tests, 1 Fehler, 3 übersprungen.
  - Der Fehler ist `test_connections_firmware…test_actual_bootstrap_success_refusal_and_rollback` („offline image must provide both shells“, braucht BusyBox `ash`). **[belegt]** Er tritt identisch auf unverändertem `develop` auf (46 Tests, gleicher Fehler) und ist umgebungsbedingt.
  - Neu und bestanden: `test_base_profiles.py` und `test_inspect_base.py`.
- `launch_helpers/tests/test_launcher.py`: PASS. Auf `develop` schlug der Test unter macOS wegen `/var` → `/private/var` fehl; der Test-Root wird jetzt aufgelöst.
- CC2 Control `make clean test CROSS= CC=gcc`: lässt sich unter macOS nicht bauen (`O_NOFOLLOW` u.a. Linux-Header in `src/network_status.c`). `cc2-control/` wurde nicht geändert. Ausführen unter Linux bzw. in der CI.
- Trockenlauf:
  - `firmware_builder.py --preflight-only` bricht wie erwartet ab: „Base profile 02.03.03.13 has 18 open placeholder(s); refusing to build“.
  - Mit `--base-version 02.01.01.07` wird das Profil akzeptiert, und der Lauf scheitert erwartungsgemäß am fehlenden lokalen Signierschlüssel; Paket, AES-Schlüssel und SquashFS liegen ebenfalls nicht vor.
- PowerShell-Änderung (`Build-Release.ps1`) nicht ausgeführt: `pwsh` ist nicht installiert.

---

## Anhang A – Versions-Fundstellen (grep `0?2.0?1.0?[01].x`)

| Datei | Zeile | Art | Vorschlag |
|---|---|---|---|
| `.gitattributes` | 25 | Version 02.01.01.07 | Kommentar – unverändert |
| `CHANGELOG.md` | 214 | Version 02.01.01.07 | Historie – nicht ändern |
| `CHANGELOG.md` | 361 | Version 02.01.01.07 | Historie – nicht ändern |
| `CHANGELOG.md` | 400 | Version 02.01.01.07 | Historie – nicht ändern |
| `LEGGIMI-RELEASE.txt` | 10 | Version 02.01.01.07 | Doku – erst nach Hardware-Test auf 02.03.03.13 anpassen |
| `LEGGIMI-RELEASE.txt` | 12 | Version 02.01.01.07 | Doku – erst nach Hardware-Test auf 02.03.03.13 anpassen |
| `NOTICE.md` | 12 | Version 02.01.00.00 | Doku – erst nach Hardware-Test auf 02.03.03.13 anpassen |
| `README.it.md` | 50 | Version 02.01.01.07 | Doku – erst nach Hardware-Test auf 02.03.03.13 anpassen |
| `README.md` | 123 | Version 02.01.01.07 | Doku – erst nach Hardware-Test auf 02.03.03.13 anpassen |
| `builder/current/Build-Release.ps1` | 48 | Version 02.01.01.07 | Aus base_profiles/<version>.json lesen (Schritt 4) |
| `builder/current/README.md` | 4 | Version 02.01.01.07 | Doku – erst nach Hardware-Test auf 02.03.03.13 anpassen |
| `builder/current/README.md` | 58 | Version 02.01.01.07 | Doku – erst nach Hardware-Test auf 02.03.03.13 anpassen |
| `builder/current/components/printer/reference.json` | 2 | Version 02.01.00.00 | Ältere Basis (Historie) – nicht ändern |
| `builder/current/core/firmware_builder.py` | 2 | Version 02.01.01.07 | Aus base_profiles/<version>.json lesen (Schritt 4) |
| `builder/current/core/firmware_builder.py` | 9 | Version 02.01.01.07 | Aus base_profiles/<version>.json lesen (Schritt 4) |
| `builder/current/core/firmware_builder.py` | 237 | Version 02.01.01.07 | Aus base_profiles/<version>.json lesen (Schritt 4) |
| `builder/current/core/firmware_builder.py` | 454 | Version 02.01.01.07 | Aus base_profiles/<version>.json lesen (Schritt 4) |
| `builder/current/core/firmware_builder.py` | 456 | Version 02.01.01.07 | Aus base_profiles/<version>.json lesen (Schritt 4) |
| `builder/current/core/firmware_builder.py` | 603 | Version 02.01.01.07 | Aus base_profiles/<version>.json lesen (Schritt 4) |
| `builder/current/core/firmware_builder.py` | 605 | Version 02.01.01.07 | Aus base_profiles/<version>.json lesen (Schritt 4) |
| `builder/current/core/firmware_builder.py` | 619 | Version 02.01.01.07 | Aus base_profiles/<version>.json lesen (Schritt 4) |
| `builder/current/core/firmware_builder.py` | 623 | Version 02.01.01.07 | Aus base_profiles/<version>.json lesen (Schritt 4) |
| `builder/current/dualtrust/apply_dualtrust.py` | 64 | Version 02.01.00.00 | Ältere Basis (Historie) – nicht ändern |
| `builder/current/launch_helpers/run_build.py` | 46 | Version 02.01.01.07 | Aus base_profiles/<version>.json lesen (Schritt 4) |
| `builder/current/launch_helpers/tests/test_launcher.py` | 9 | Version 02.01.01.07 | Test an Profil-Mechanismus anpassen |
| `builder/current/launch_helpers/tests/test_launcher.py` | 18 | Version 02.01.01.07 | Test an Profil-Mechanismus anpassen |
| `builder/current/prepare_reactor.py` | 2 | Version 02.01.01.07 | prüfen |
| `builder/current/prepare_vendor.py` | 21 | Version 02.01.01.07 | Aus base_profiles/<version>.json lesen (Schritt 4) |
| `builder/current/tests/test_44_port_static.py` | 10 | Version 02.01.01.07 | Test an Profil-Mechanismus anpassen |
| `builder/current/tests/test_44_port_static.py` | 25 | Version 02.01.01.07 | Test an Profil-Mechanismus anpassen |
| `builder/current/tests/test_44_port_static.py` | 26 | Version 02.01.00.00 | Ältere Basis (Historie) – nicht ändern |
| `cc2-control/docs/APPLICATION_UPDATES.it.md` | 6 | Version 02.01.01.07 | Doku – erst nach Hardware-Test auf 02.03.03.13 anpassen |
| `cc2-control/docs/APPLICATION_UPDATES.md` | 5 | Version 02.01.01.07 | Doku – erst nach Hardware-Test auf 02.03.03.13 anpassen |
| `cc2-control/docs/COMBINED_TEST.md` | 4 | Version 02.01.01.07 | Doku – erst nach Hardware-Test auf 02.03.03.13 anpassen |
| `cc2-control/docs/EXTERNAL_FILAMENT.md` | 15 | Version 02.01.01.07 | Doku – erst nach Hardware-Test auf 02.03.03.13 anpassen |
| `cc2-control/installer/README.md` | 4 | Version 02.01.01.07 | Doku – erst nach Hardware-Test auf 02.03.03.13 anpassen |
| `cc2-control/installer/compatibility.json` | 3 | Version 02.01.01.07 | CC2 Control: Kommentar/Fingerprint – erst mit echten 02.03-Hashes anpassen |
| `cc2-control/src/canvas_unload.h` | 30 | Version 02.01.01.07 | CC2 Control: Kommentar/Fingerprint – erst mit echten 02.03-Hashes anpassen |
| `cc2-control/src/external_filament_catalog.h` | 1 | Version 02.01.01.07 | CC2 Control: Kommentar/Fingerprint – erst mit echten 02.03-Hashes anpassen |
| `cc2-control/tests/test_runout.c` | 6 | Version 02.01.01.07 | CC2 Control: Kommentar/Fingerprint – erst mit echten 02.03-Hashes anpassen |
| `docs/BUILD.md` | 3 | Version 02.01.01.07 | Doku – erst nach Hardware-Test auf 02.03.03.13 anpassen |
| `docs/CALLBACK_RUNTIME.md` | 13 | Version 02.01.01.07 | Doku – erst nach Hardware-Test auf 02.03.03.13 anpassen |
| `docs/CC2_RESEARCH.md` | 19 | Version 02.01.00.00 | Doku – erst nach Hardware-Test auf 02.03.03.13 anpassen |
| `docs/FIRST_RUN.it.md` | 5 | Version 02.01.01.07 | Doku – erst nach Hardware-Test auf 02.03.03.13 anpassen |
| `docs/FIRST_RUN.it.md` | 20 | Version 02.01.01.07 | Doku – erst nach Hardware-Test auf 02.03.03.13 anpassen |
| `docs/FIRST_RUN.it.md` | 37 | Version 02.01.01.07 | Doku – erst nach Hardware-Test auf 02.03.03.13 anpassen |
| `docs/FIRST_RUN.it.md` | 94 | Version 02.01.01.07 | Doku – erst nach Hardware-Test auf 02.03.03.13 anpassen |
| `docs/FIRST_RUN.it.md` | 98 | Version 02.01.00.00 | Doku – erst nach Hardware-Test auf 02.03.03.13 anpassen |
| `docs/FIRST_RUN.it.md` | 98 | Version 02.01.01.07 | Doku – erst nach Hardware-Test auf 02.03.03.13 anpassen |
| `docs/FIRST_RUN.md` | 5 | Version 02.01.01.07 | Doku – erst nach Hardware-Test auf 02.03.03.13 anpassen |
| `docs/FIRST_RUN.md` | 20 | Version 02.01.01.07 | Doku – erst nach Hardware-Test auf 02.03.03.13 anpassen |
| `docs/FIRST_RUN.md` | 37 | Version 02.01.01.07 | Doku – erst nach Hardware-Test auf 02.03.03.13 anpassen |
| `docs/FIRST_RUN.md` | 94 | Version 02.01.01.07 | Doku – erst nach Hardware-Test auf 02.03.03.13 anpassen |
| `docs/FIRST_RUN.md` | 98 | Version 02.01.00.00 | Doku – erst nach Hardware-Test auf 02.03.03.13 anpassen |
| `docs/FIRST_RUN.md` | 98 | Version 02.01.01.07 | Doku – erst nach Hardware-Test auf 02.03.03.13 anpassen |
| `docs/INSTALL.md` | 19 | Version 02.01.01.07 | Doku – erst nach Hardware-Test auf 02.03.03.13 anpassen |
| `docs/INSTALL.md` | 23 | Version 02.01.00.00 | Doku – erst nach Hardware-Test auf 02.03.03.13 anpassen |
| `docs/INSTALL.md` | 23 | Version 02.01.01.07 | Doku – erst nach Hardware-Test auf 02.03.03.13 anpassen |
| `docs/UPDATE_CC2.it.md` | 4 | Version 02.01.01.07 | Doku – erst nach Hardware-Test auf 02.03.03.13 anpassen |
| `docs/UPDATE_CC2.md` | 4 | Version 02.01.01.07 | Doku – erst nach Hardware-Test auf 02.03.03.13 anpassen |
| `experimental/camera-client-reap-r1/README.en.md` | 3 | Version 02.01.01.07 | Doku – erst nach Hardware-Test auf 02.03.03.13 anpassen |
| `experimental/camera-client-reap-r1/README.it.md` | 3 | Version 02.01.01.07 | Doku – erst nach Hardware-Test auf 02.03.03.13 anpassen |
| `experimental/camera-persistent-r2/VALIDATION.md` | 3 | Version 02.01.01.07 | Doku – erst nach Hardware-Test auf 02.03.03.13 anpassen |
| `experimental/firmware-port/README.md` | 1 | Version 02.01.01.07 | Doku – erst nach Hardware-Test auf 02.03.03.13 anpassen |
| `experimental/firmware-port/README.md` | 3 | Version 02.01.01.07 | Doku – erst nach Hardware-Test auf 02.03.03.13 anpassen |
| `experimental/firmware-port/README.md` | 11 | Version 02.01.01.07 | Doku – erst nach Hardware-Test auf 02.03.03.13 anpassen |
| `experimental/firmware-port/assemble_dual44_gnu.py` | 2 | Version 02.01.01.07 | Port-Satz 02.01.01.07 – bleibt; neuer Port-Satz für 02.03.03.13 nötig (Reverse Engineering) |
| `experimental/firmware-port/baseline-02.01.01.07.json` | 2 | Version 02.01.01.07 | Port-Satz 02.01.01.07 – bleibt; neuer Port-Satz für 02.03.03.13 nötig (Reverse Engineering) |
| `experimental/firmware-port/patch_dual44.py` | 2 | Version 02.01.01.07 | Port-Satz 02.01.01.07 – bleibt; neuer Port-Satz für 02.03.03.13 nötig (Reverse Engineering) |
| `experimental/firmware-port/patch_zoffset44.py` | 2 | Version 02.01.01.07 | Port-Satz 02.01.01.07 – bleibt; neuer Port-Satz für 02.03.03.13 nötig (Reverse Engineering) |

## Anhang B – SHA256-Pins und Offsets im Build-Pfad

Eigenbau-Pins (sshd, Patcher, Tools, CC2-Control-Snapshot) sind unabhängig von der Vendor-Basis. Sie bleiben, solange sich die Komponenten nicht ändern.

| Datei | Zeile | Art | Vorschlag |
|---|---|---|---|
| `builder/current/components/printer/reference.json` | 4 | SHA256-Pin | Historische 02.01.00.00-Referenz; unverändert. Kontext: `"input_sha256": "0304ea0cbef4e63baa32f98680076c683c18c0d9255c1c66ac3c3` |
| `builder/current/components/printer/reference.json` | 6 | SHA256-Pin | Historische 02.01.00.00-Referenz; unverändert. Kontext: `"output_sha256": "c21126e78a63ac9140e7347c56f363d5e513495c58c06fc17e8e` |
| `builder/current/components/printer/reference.json` | 10 | Offset/VA | Historische 02.01.00.00-Referenz; unverändert. Kontext: `"virtual_address": "0x5c3ca8",` |
| `builder/current/components/printer/reference.json` | 11 | Offset/VA | Historische 02.01.00.00-Referenz; unverändert. Kontext: `"file_offset": "0x5b3ca8",` |
| `builder/current/core/camera_component.py` | 13 | SHA256-Pin | Vendor-Pin → base_profiles/*.json bzw. Portierung; Eigenbau-Pins (Tools/Komponenten) bleiben. Kontext: `CAMERA_SHA='ec5633b17de329d5b18c3f8d8de6ebf389391a04193536d976ba167022` |
| `builder/current/core/camera_component.py` | 14 | SHA256-Pin | Vendor-Pin → base_profiles/*.json bzw. Portierung; Eigenbau-Pins (Tools/Komponenten) bleiben. Kontext: `INIT_SHA='bcabb1fa405b066b06c229c6d8cc6e474a0ccc57e31a1b5b387d3b55945b` |
| `builder/current/core/connections_component.py` | 15 | SHA256-Pin | Vendor-Pin → base_profiles/*.json bzw. Portierung; Eigenbau-Pins (Tools/Komponenten) bleiben. Kontext: `RUNTIME_SHA256 = '649d0af3dba7e21bc399fc741115fbe298cc1e4b77c07cf51bd3` |
| `builder/current/core/connections_component.py` | 17 | SHA256-Pin | Vendor-Pin → base_profiles/*.json bzw. Portierung; Eigenbau-Pins (Tools/Komponenten) bleiben. Kontext: `'control': '2a4dd2dd8e23aa3446348973ba6b8fa90a1fda16fcd96626015f8f0b81` |
| `builder/current/core/connections_component.py` | 18 | SHA256-Pin | Vendor-Pin → base_profiles/*.json bzw. Portierung; Eigenbau-Pins (Tools/Komponenten) bleiben. Kontext: `'cc2_ue300_abi_probe.ko': '24c757788815abe5bbabb59d64b4768efedf7a65b1a` |
| `builder/current/core/connections_component.py` | 19 | SHA256-Pin | Vendor-Pin → base_profiles/*.json bzw. Portierung; Eigenbau-Pins (Tools/Komponenten) bleiben. Kontext: `'cc2_ue300_abi_probe_3000.ko': '4027162b4a5a45fc136b932f4a33ee2a2f2522` |
| `builder/current/core/connections_component.py` | 20 | SHA256-Pin | Vendor-Pin → base_profiles/*.json bzw. Portierung; Eigenbau-Pins (Tools/Komponenten) bleiben. Kontext: `'cc2_ue300_mii.ko': '9051751ccefa1c1795b644764fd4c2de31b54d871e863927e` |
| `builder/current/core/connections_component.py` | 21 | SHA256-Pin | Vendor-Pin → base_profiles/*.json bzw. Portierung; Eigenbau-Pins (Tools/Komponenten) bleiben. Kontext: `'cc2_ue300_r8152.ko': '5118667293fe05753f38c31bafe654d110104c6d7239ba8` |
| `builder/current/core/connections_component.py` | 22 | SHA256-Pin | Vendor-Pin → base_profiles/*.json bzw. Portierung; Eigenbau-Pins (Tools/Komponenten) bleiben. Kontext: `'libcc2-network-compat-r1.so': 'bde512195dd6bf3846f3103b5e1acbd89a62e5` |
| `builder/current/core/connections_component.py` | 93 | SHA256-Pin | Vendor-Pin → base_profiles/*.json bzw. Portierung; Eigenbau-Pins (Tools/Komponenten) bleiben. Kontext: `'bin/wifi_daemon': '8e19a620921c6f4f21bde910cc3d960af5400d223493041eb9` |
| `builder/current/core/connections_component.py` | 94 | SHA256-Pin | Vendor-Pin → base_profiles/*.json bzw. Portierung; Eigenbau-Pins (Tools/Komponenten) bleiben. Kontext: `'lib/modules/5.4.61/aic8800_fdrv.ko': '2973d7d7856b612ed5880fefa5b8f33` |
| `builder/current/core/connections_component.py` | 95 | SHA256-Pin | Vendor-Pin → base_profiles/*.json bzw. Portierung; Eigenbau-Pins (Tools/Komponenten) bleiben. Kontext: `'etc/init.d/printer': 'e7537298955e23b7a03cab4c8d7bd241c1dfcc070883cdf` |
| `builder/current/core/connections_component.py` | 96 | SHA256-Pin | Vendor-Pin → base_profiles/*.json bzw. Portierung; Eigenbau-Pins (Tools/Komponenten) bleiben. Kontext: `'etc/wifi/udhcpc_wlan0': 'b76f68e345f1221d835d1a0d6279827c5df4823c2e7f` |
| `builder/current/core/connections_component.py` | 97 | SHA256-Pin | Vendor-Pin → base_profiles/*.json bzw. Portierung; Eigenbau-Pins (Tools/Komponenten) bleiben. Kontext: `'usr/share/udhcpc/set_lan.sh': '53cf6af55ad333d5781b0d48d89fd48bde22bc` |
| `builder/current/core/connections_component.py` | 98 | SHA256-Pin | Vendor-Pin → base_profiles/*.json bzw. Portierung; Eigenbau-Pins (Tools/Komponenten) bleiben. Kontext: `'etc/init.d/cc2-camera-launch': 'f05accc97f0195cf4cf481b53b64390242418` |
| `builder/current/core/connections_component.py` | 99 | SHA256-Pin | Vendor-Pin → base_profiles/*.json bzw. Portierung; Eigenbau-Pins (Tools/Komponenten) bleiben. Kontext: `'etc/init.d/cc2-reactor-launch': '8d4a19c9be1f6f85fe5ab9d3f116aa84400e` |
| `builder/current/core/firmware_builder.py` | 27 | SHA256-Pin | Vendor-Pin → base_profiles/*.json bzw. Portierung; Eigenbau-Pins (Tools/Komponenten) bleiben. Kontext: `"stock_swu":"0654a0de241415d5e168fae21faf4384a34dbbf59968ef48dc5102c3e` |
| `builder/current/core/firmware_builder.py` | 28 | SHA256-Pin | Vendor-Pin → base_profiles/*.json bzw. Portierung; Eigenbau-Pins (Tools/Komponenten) bleiben. Kontext: `"sw-description":"6598586ada5e7e9256316c63b801579e65e797d2a77e13ab360f` |
| `builder/current/core/firmware_builder.py` | 29 | SHA256-Pin | Vendor-Pin → base_profiles/*.json bzw. Portierung; Eigenbau-Pins (Tools/Komponenten) bleiben. Kontext: `"resource":"aacfe9a9d847f4be3e4b5a16f7c16cba93a60e25ba371efb77568568b0` |
| `builder/current/core/firmware_builder.py` | 30 | SHA256-Pin | Vendor-Pin → base_profiles/*.json bzw. Portierung; Eigenbau-Pins (Tools/Komponenten) bleiben. Kontext: `"uboot":"769d55b96ed4dff486ff5b79f35d61a36d68c444f49f19e44059e2472d468` |
| `builder/current/core/firmware_builder.py` | 31 | SHA256-Pin | Vendor-Pin → base_profiles/*.json bzw. Portierung; Eigenbau-Pins (Tools/Komponenten) bleiben. Kontext: `"boot0":"b18b3f31b3d378b56b4d01b1863a6b9f0f61211ba1f4ad8e7b672ff54887e` |
| `builder/current/core/firmware_builder.py` | 32 | SHA256-Pin | Vendor-Pin → base_profiles/*.json bzw. Portierung; Eigenbau-Pins (Tools/Komponenten) bleiben. Kontext: `"kernel":"e1ca036e667f1976fb1a1944b6922df8a1c26d5870912dd8273e0fcc3bb4` |
| `builder/current/core/firmware_builder.py` | 33 | SHA256-Pin | Vendor-Pin → base_profiles/*.json bzw. Portierung; Eigenbau-Pins (Tools/Komponenten) bleiben. Kontext: `"stock_rootfs":"20375a79e1ee2748f43fd39a62e5a48b23b945ed048dbe52a54d0a` |
| `builder/current/core/firmware_builder.py` | 34 | SHA256-Pin | Vendor-Pin → base_profiles/*.json bzw. Portierung; Eigenbau-Pins (Tools/Komponenten) bleiben. Kontext: `"stock_gui":"24d0e561287a4b5a87c8ec698c98f60acf2c70511114b36224b4eb924` |
| `builder/current/core/firmware_builder.py` | 35 | SHA256-Pin | Vendor-Pin → base_profiles/*.json bzw. Portierung; Eigenbau-Pins (Tools/Komponenten) bleiben. Kontext: `"patched_gui":"5f3122852044f6750dbd36aeb2ebcbd0ea8c5e99f48a52c186def1f` |
| `builder/current/core/firmware_builder.py` | 36 | SHA256-Pin | Vendor-Pin → base_profiles/*.json bzw. Portierung; Eigenbau-Pins (Tools/Komponenten) bleiben. Kontext: `"sshd":"7746b7085a1539f0a8db1aea89a97ea0ed7e8e5e722fecd476fdedfa1e7a00` |
| `builder/current/core/firmware_builder.py` | 37 | SHA256-Pin | Vendor-Pin → base_profiles/*.json bzw. Portierung; Eigenbau-Pins (Tools/Komponenten) bleiben. Kontext: `"sshd_init":"69b42d5f0adf20eeffd5218bc9e955af1d3590d68a07c21ad90128ea7` |
| `builder/current/core/firmware_builder.py` | 38 | SHA256-Pin | Vendor-Pin → base_profiles/*.json bzw. Portierung; Eigenbau-Pins (Tools/Komponenten) bleiben. Kontext: `"sshd_config":"0b8f561d681101fff24bb5f65f469f6ea8f838d5c12576f104b73c8` |
| `builder/current/core/firmware_builder.py` | 39 | SHA256-Pin | Vendor-Pin → base_profiles/*.json bzw. Portierung; Eigenbau-Pins (Tools/Komponenten) bleiben. Kontext: `"daemon_stock":"6fed6657e814f010fef87b40c5932d8fa7c5e9c044d7376f25fc23` |
| `builder/current/core/firmware_builder.py` | 40 | SHA256-Pin | Vendor-Pin → base_profiles/*.json bzw. Portierung; Eigenbau-Pins (Tools/Komponenten) bleiben. Kontext: `"dual_payload":"04b9f093f439c336476f2a34f109b088b05e6c42a4f1baf2f10e79` |
| `builder/current/core/firmware_builder.py` | 41 | SHA256-Pin | Vendor-Pin → base_profiles/*.json bzw. Portierung; Eigenbau-Pins (Tools/Komponenten) bleiben. Kontext: `"daemon_dual":"83433b14e49d1e8aa9523282ead03618378573f4b66de1ad8f7f3c4` |
| `builder/current/core/firmware_builder.py` | 43 | SHA256-Pin | Vendor-Pin → base_profiles/*.json bzw. Portierung; Eigenbau-Pins (Tools/Komponenten) bleiben. Kontext: `"stock_outer":"278a0606a072ae432e9bf2d4f0157acba4570d1c85dbc768c359fef` |
| `builder/current/core/firmware_builder.py` | 44 | SHA256-Pin | Vendor-Pin → base_profiles/*.json bzw. Portierung; Eigenbau-Pins (Tools/Komponenten) bleiben. Kontext: `"stock_inner":"5ae3d2e392fa47d7b0bc42981389fd84d702ad069d535c1392d50a7` |
| `builder/current/core/firmware_builder.py` | 45 | SHA256-Pin | Vendor-Pin → base_profiles/*.json bzw. Portierung; Eigenbau-Pins (Tools/Komponenten) bleiben. Kontext: `"aes_key":"70adfb48a2156086ad2495cfc121557f455dd5373450017d8da1dcacd7c` |
| `builder/current/core/firmware_builder.py` | 46 | SHA256-Pin | Vendor-Pin → base_profiles/*.json bzw. Portierung; Eigenbau-Pins (Tools/Komponenten) bleiben. Kontext: `"zoffset_patcher":"a233e6eb18993bf2fa82534ceca3e351ccf7069654c7f28c01b` |
| `builder/current/core/firmware_builder.py` | 47 | SHA256-Pin | Vendor-Pin → base_profiles/*.json bzw. Portierung; Eigenbau-Pins (Tools/Komponenten) bleiben. Kontext: `"dual_patcher":"32c2e71964b9194da02182d145cf656e8b788cd9460d8b12e8a27a` |
| `builder/current/core/firmware_builder.py` | 48 | SHA256-Pin | Vendor-Pin → base_profiles/*.json bzw. Portierung; Eigenbau-Pins (Tools/Komponenten) bleiben. Kontext: `"sig_tool":"704aab8e544d3aa92b3041183c431a544c17b95f8dcd766367abf71663` |
| `builder/current/core/firmware_builder.py` | 49 | SHA256-Pin | Vendor-Pin → base_profiles/*.json bzw. Portierung; Eigenbau-Pins (Tools/Komponenten) bleiben. Kontext: `"mksquashfs":"6b457f6a5588aa66092aad192c09072e2de597d21d5ba990dee87835` |
| `builder/current/core/firmware_builder.py` | 50 | SHA256-Pin | Vendor-Pin → base_profiles/*.json bzw. Portierung; Eigenbau-Pins (Tools/Komponenten) bleiben. Kontext: `"unsquashfs":"6b0d44d206a64f534e79e4a8062f2e70854fa464de0c9b83ae8eb19d` |
| `builder/current/core/firmware_builder.py` | 53 | SHA256-Pin | Vendor-Pin → base_profiles/*.json bzw. Portierung; Eigenbau-Pins (Tools/Komponenten) bleiben. Kontext: `"rootfs":"b9e46320d01d6c3fcc5e35a67c74e81df506afaba754b7a69b5502c78871` |
| `builder/current/core/firmware_builder.py` | 54 | SHA256-Pin | Vendor-Pin → base_profiles/*.json bzw. Portierung; Eigenbau-Pins (Tools/Komponenten) bleiben. Kontext: `"swu":"d3c0f5287c61cde457938a7c18600e0148873618c5f10f3acff8de1a50f92a8` |
| `builder/current/core/firmware_builder.py` | 55 | SHA256-Pin | Vendor-Pin → base_profiles/*.json bzw. Portierung; Eigenbau-Pins (Tools/Komponenten) bleiben. Kontext: `"swusig":"b6872abd5131fbbdb8075619c56a1e5908d3d601f7698310bb1c698cfd5f` |
| `builder/current/core/firmware_builder.py` | 56 | SHA256-Pin | Vendor-Pin → base_profiles/*.json bzw. Portierung; Eigenbau-Pins (Tools/Komponenten) bleiben. Kontext: `"zip":"564ce9c760763109dd1c876b072357000f58622fa7f04ca8ccbbeacc7be8117` |
| `builder/current/core/firmware_builder.py` | 57 | SHA256-Pin | Vendor-Pin → base_profiles/*.json bzw. Portierung; Eigenbau-Pins (Tools/Komponenten) bleiben. Kontext: `"outer":"eeadf35d6310b7f847bc064d744c01fedf918e0b34e100cde37a3a0b2da0f` |
| `builder/current/core/firmware_builder.py` | 59 | Offset/VA | Containerformat/Patch-Offset; mit inspect_base.py gegen 02.03 prüfen. Kontext: `HOOK_OFFSET=0x2C68C; INJECT_OFFSET=0x852AC; PAYLOAD_SIZE=0x318` |
| `builder/current/core/firmware_builder.py` | 60 | Offset/VA | Containerformat/Patch-Offset; mit inspect_base.py gegen 02.03 prüfen. Kontext: `ELF_FILESZ_OFFSET=0xA4; ELF_MEMSZ_OFFSET=0xA8; OLD_SEGMENT_SIZE=0x852A` |
| `builder/current/core/firmware_builder.py` | 83 | SHA256-Pin | Vendor-Pin → base_profiles/*.json bzw. Portierung; Eigenbau-Pins (Tools/Komponenten) bleiben. Kontext: `CC2_CONTROL_SOURCE_SHA256='aeb57f381ce6afaa1e9b44c30a2f92b54ad8a43c24a` |
| `builder/current/core/firmware_builder.py` | 318 | SHA256-Pin ×2 | Vendor-Pin → base_profiles/*.json bzw. Portierung; Eigenbau-Pins (Tools/Komponenten) bleiben. Kontext: `PUBLIC_HASHES={'stock': '39f285d0fac0a14d5cec7e07e5c3a832e31f208de83fe` |
| `builder/current/core/firmware_builder.py` | 363 | SHA256-Pin | Vendor-Pin → base_profiles/*.json bzw. Portierung; Eigenbau-Pins (Tools/Komponenten) bleiben. Kontext: `PRINTER_STOCK_SHA256="c968ad1430f331bf0a20d5e54bdc8c1428a223b5977c3320` |
| `builder/current/core/firmware_builder.py` | 364 | SHA256-Pin | Vendor-Pin → base_profiles/*.json bzw. Portierung; Eigenbau-Pins (Tools/Komponenten) bleiben. Kontext: `PRINTER_PATCHED_SHA256="fb95bd123fbbb9e77ed380a0ab7bdb53a0b86997af8452` |
| `builder/current/core/firmware_builder.py` | 367 | SHA256-Pin | Vendor-Pin → base_profiles/*.json bzw. Portierung; Eigenbau-Pins (Tools/Komponenten) bleiben. Kontext: `HTTP_UPLOAD_MANIFEST_SHA256="3f41a27fa289fd4f900e374e77b980b970740f066` |
| `builder/current/core/firmware_builder.py` | 399 | SHA256-Pin | Vendor-Pin → base_profiles/*.json bzw. Portierung; Eigenbau-Pins (Tools/Komponenten) bleiben. Kontext: `PRINTER_RELEASE_SHA256="19712b5943b10bb8cb6aab9a4ed498ee962b28538b23c9` |
| `builder/current/core/firmware_builder.py` | 415 | SHA256-Pin | Vendor-Pin → base_profiles/*.json bzw. Portierung; Eigenbau-Pins (Tools/Komponenten) bleiben. Kontext: `REACTOR_HELPER_SHA256='d9af4d96820846d0fab1f6dfe5ef91c10221cbf6196653e` |
| `builder/current/core/firmware_builder.py` | 416 | SHA256-Pin | Vendor-Pin → base_profiles/*.json bzw. Portierung; Eigenbau-Pins (Tools/Komponenten) bleiben. Kontext: `CAMERA_HELPER_SHA256='74b8865cdb1fe12df4bcc54670a5b85bab7333ddbcf3959b` |
| `builder/current/core/firmware_builder.py` | 417 | SHA256-Pin ×3 | Vendor-Pin → base_profiles/*.json bzw. Portierung; Eigenbau-Pins (Tools/Komponenten) bleiben. Kontext: `INTEGRATION_HELPERS={'connections_component.py': '05ec5c63170cc861b76a` |
| `builder/current/core/firmware_builder.py` | 418 | SHA256-Pin ×13 | Vendor-Pin → base_profiles/*.json bzw. Portierung; Eigenbau-Pins (Tools/Komponenten) bleiben. Kontext: `INTEGRATION_INPUTS={'experimental/native-display/package.py': 'fccf9f4` |
| `builder/current/core/firmware_builder.py` | 433 | SHA256-Pin | Vendor-Pin → base_profiles/*.json bzw. Portierung; Eigenbau-Pins (Tools/Komponenten) bleiben. Kontext: `CAMERA_RELEASE_SHA256="ec5633b17de329d5b18c3f8d8de6ebf389391a04193536d` |
| `builder/current/core/firmware_builder.py` | 434 | SHA256-Pin ×10 | Vendor-Pin → base_profiles/*.json bzw. Portierung; Eigenbau-Pins (Tools/Komponenten) bleiben. Kontext: `PORT_HASHES={'patch_camera44.py': '63ef66a15ed9a12d67fb0c6d919d249da7d` |
| `builder/current/core/firmware_builder.py` | 544 | SHA256-Pin | Vendor-Pin → base_profiles/*.json bzw. Portierung; Eigenbau-Pins (Tools/Komponenten) bleiben. Kontext: `reqhash(printer,'8039081f9c4b01e720d838d4ab6e4515a8979b9a71a33bd416432` |
| `builder/current/core/native_display_component.py` | 20 | SHA256-Pin | Vendor-Pin → base_profiles/*.json bzw. Portierung; Eigenbau-Pins (Tools/Komponenten) bleiben. Kontext: `GUI_SHA256 = 'db09fa37d378df259427aaac392a341282e08ceb26cd87603542c0dc` |
| `builder/current/core/native_display_component.py` | 26 | SHA256-Pin | Vendor-Pin → base_profiles/*.json bzw. Portierung; Eigenbau-Pins (Tools/Komponenten) bleiben. Kontext: `'20964211688a113d7c539ad1c3fd240d7e32c7393aae59bbb0ed15fd10d7af57',` |
| `builder/current/core/native_display_component.py` | 28 | SHA256-Pin | Vendor-Pin → base_profiles/*.json bzw. Portierung; Eigenbau-Pins (Tools/Komponenten) bleiben. Kontext: `'32d03b7b46acc68f5e0e0dedfb5930a8f091d44cc185c4036477d89f9e076ffb',` |
| `builder/current/core/reactor_component.py` | 10 | SHA256-Pin | Vendor-Pin → base_profiles/*.json bzw. Portierung; Eigenbau-Pins (Tools/Komponenten) bleiben. Kontext: `VENDOR_SHA256='19712b5943b10bb8cb6aab9a4ed498ee962b28538b23c91b7ed003e` |
| `builder/current/core/reactor_component.py` | 11 | SHA256-Pin | Vendor-Pin → base_profiles/*.json bzw. Portierung; Eigenbau-Pins (Tools/Komponenten) bleiben. Kontext: `LIBCO_SHA256='14288a4a73c339b039dd89597618ebb05702f90d668b76f74019d5d1` |
| `builder/current/core/reactor_component.py` | 12 | SHA256-Pin | Vendor-Pin → base_profiles/*.json bzw. Portierung; Eigenbau-Pins (Tools/Komponenten) bleiben. Kontext: `FIRMWARE_LAUNCHER_SHA256='8d4a19c9be1f6f85fe5ab9d3f116aa84400e7e6a77c4` |
| `builder/current/core/resource_update.py` | 8 | SHA256-Pin | Vendor-Pin → base_profiles/*.json bzw. Portierung; Eigenbau-Pins (Tools/Komponenten) bleiben. Kontext: `STOCK_SHA256='6598586ada5e7e9256316c63b801579e65e797d2a77e13ab360f09b3` |
| `builder/current/dualtrust/apply_dualtrust.py` | 7 | SHA256-Pin | Gehört zum 02.01.01.07-Patch-Satz; für 02.03.03.13 neu ermitteln (Portierung). Kontext: `STOCK_SHA256 = "b760fd80bb03de28ac348756a9a1c5311066377876570d2fdfd45f` |
| `builder/current/dualtrust/apply_dualtrust.py` | 8 | SHA256-Pin | Gehört zum 02.01.01.07-Patch-Satz; für 02.03.03.13 neu ermitteln (Portierung). Kontext: `PAYLOAD_SHA256 = "33d013ca4a63334fdbbd0adb77bd10f81e2e21c3ac41229e216e` |
| `builder/current/dualtrust/apply_dualtrust.py` | 9 | SHA256-Pin | Gehört zum 02.01.01.07-Patch-Satz; für 02.03.03.13 neu ermitteln (Portierung). Kontext: `FINAL_SHA256 = "3af6104d0ac76cc043ecf38985e1b00a0d5ceace0a4f4b66820e21` |
| `builder/current/dualtrust/apply_dualtrust.py` | 11 | Offset/VA | Gehört zum 02.01.01.07-Patch-Satz; für 02.03.03.13 neu ermitteln (Portierung). Kontext: `HOOK_OFFSET = 0x2C68C` |
| `builder/current/dualtrust/apply_dualtrust.py` | 12 | Offset/VA | Gehört zum 02.01.01.07-Patch-Satz; für 02.03.03.13 neu ermitteln (Portierung). Kontext: `INJECT_OFFSET = 0x852AC` |
| `builder/current/dualtrust/apply_dualtrust.py` | 15 | Offset/VA | Gehört zum 02.01.01.07-Patch-Satz; für 02.03.03.13 neu ermitteln (Portierung). Kontext: `ELF_FILESZ_OFFSET = 0xA4` |
| `builder/current/dualtrust/apply_dualtrust.py` | 16 | Offset/VA | Gehört zum 02.01.01.07-Patch-Satz; für 02.03.03.13 neu ermitteln (Portierung). Kontext: `ELF_MEMSZ_OFFSET = 0xA8` |
| `builder/current/dualtrust/apply_dualtrust.py` | 79 | Offset/VA | Gehört zum 02.01.01.07-Patch-Satz; für 02.03.03.13 neu ermitteln (Portierung). Kontext: `if data[HOOK_OFFSET:HOOK_OFFSET + 4] != STOCK_HOOK:` |
| `builder/current/dualtrust/apply_dualtrust.py` | 82 | Offset/VA | Gehört zum 02.01.01.07-Patch-Satz; für 02.03.03.13 neu ermitteln (Portierung). Kontext: `cave = data[INJECT_OFFSET:INJECT_OFFSET + PAYLOAD_SIZE]` |
| `builder/current/dualtrust/apply_dualtrust.py` | 90 | Offset/VA | Gehört zum 02.01.01.07-Patch-Satz; für 02.03.03.13 neu ermitteln (Portierung). Kontext: `print(f"Hook offset : 0x{HOOK_OFFSET:X}")` |
| `builder/current/dualtrust/apply_dualtrust.py` | 91 | Offset/VA | Gehört zum 02.01.01.07-Patch-Satz; für 02.03.03.13 neu ermitteln (Portierung). Kontext: `print(f"Inject offset : 0x{INJECT_OFFSET:X}")` |
| `builder/current/dualtrust/apply_dualtrust.py` | 98 | Offset/VA | Gehört zum 02.01.01.07-Patch-Satz; für 02.03.03.13 neu ermitteln (Portierung). Kontext: `data[ELF_FILESZ_OFFSET:ELF_FILESZ_OFFSET + 4], "little"` |
| `builder/current/dualtrust/apply_dualtrust.py` | 101 | Offset/VA | Gehört zum 02.01.01.07-Patch-Satz; für 02.03.03.13 neu ermitteln (Portierung). Kontext: `data[ELF_MEMSZ_OFFSET:ELF_MEMSZ_OFFSET + 4], "little"` |
| `builder/current/dualtrust/apply_dualtrust.py` | 113 | Offset/VA | Gehört zum 02.01.01.07-Patch-Satz; für 02.03.03.13 neu ermitteln (Portierung). Kontext: `data[ELF_FILESZ_OFFSET:ELF_FILESZ_OFFSET + 4] = (` |
| `builder/current/dualtrust/apply_dualtrust.py` | 116 | Offset/VA | Gehört zum 02.01.01.07-Patch-Satz; für 02.03.03.13 neu ermitteln (Portierung). Kontext: `data[ELF_MEMSZ_OFFSET:ELF_MEMSZ_OFFSET + 4] = (` |
| `builder/current/dualtrust/apply_dualtrust.py` | 123 | Offset/VA | Gehört zum 02.01.01.07-Patch-Satz; für 02.03.03.13 neu ermitteln (Portierung). Kontext: `data[INJECT_OFFSET:INJECT_OFFSET + PAYLOAD_SIZE] = payload` |
| `builder/current/dualtrust/apply_dualtrust.py` | 124 | Offset/VA | Gehört zum 02.01.01.07-Patch-Satz; für 02.03.03.13 neu ermitteln (Portierung). Kontext: `data[HOOK_OFFSET:HOOK_OFFSET + 4] = DUAL_HOOK` |
| `builder/current/launch_helpers/run_build.py` | 17 | SHA256-Pin | Vendor-Pin → base_profiles/*.json bzw. Portierung; Eigenbau-Pins (Tools/Komponenten) bleiben. Kontext: `BUILDER_SHA256='72c763b4f584b85bd7dbf474601d811583a619035f617a9b13c914` |
| `builder/current/prepare_reactor.py` | 17 | SHA256-Pin | Vendor-Pin → base_profiles/*.json bzw. Portierung; Eigenbau-Pins (Tools/Komponenten) bleiben. Kontext: `VENDOR='19712b5943b10bb8cb6aab9a4ed498ee962b28538b23c91b7ed003eb9e5907` |
| `builder/current/prepare_reactor.py` | 18 | SHA256-Pin | Vendor-Pin → base_profiles/*.json bzw. Portierung; Eigenbau-Pins (Tools/Komponenten) bleiben. Kontext: `LIBCO='14288a4a73c339b039dd89597618ebb05702f90d668b76f74019d5d15ce6740` |
| `builder/current/prepare_vendor.py` | 13 | SHA256-Pin | Vendor-Pin → base_profiles/*.json bzw. Portierung; Eigenbau-Pins (Tools/Komponenten) bleiben. Kontext: `LIBCO_SHA='14288a4a73c339b039dd89597618ebb05702f90d668b76f74019d5d15ce` |
| `cc2-control/installer/compatibility.json` | 2 | SHA256-Pin | CC2-Control-Fingerprint; erst nach gebautem 02.03-Image ersetzen. Kontext: `"firmware_version_file_sha256": "bc33529cc1b1e6924eb04c472ecaab73efc2d` |
| `cc2-control/installer/compatibility.json` | 4 | SHA256-Pin | CC2-Control-Fingerprint; erst nach gebautem 02.03-Image ersetzen. Kontext: `"printer_sha256": "19712b5943b10bb8cb6aab9a4ed498ee962b28538b23c91b7ed` |
| `cc2-control/installer/compatibility.json` | 5 | SHA256-Pin | CC2-Control-Fingerprint; erst nach gebautem 02.03-Image ersetzen. Kontext: `"daemon_sha256": "83433b14e49d1e8aa9523282ead03618378573f4b66de1ad8f7f` |
| `experimental/firmware-port/baseline-02.01.01.07.json` | 7 | SHA256-Pin | Gehört zum 02.01.01.07-Patch-Satz; für 02.03.03.13 neu ermitteln (Portierung). Kontext: `"sha256": "278a0606a072ae432e9bf2d4f0157acba4570d1c85dbc768c359fef53ac` |
| `experimental/firmware-port/baseline-02.01.01.07.json` | 13 | SHA256-Pin | Gehört zum 02.01.01.07-Patch-Satz; für 02.03.03.13 neu ermitteln (Portierung). Kontext: `"sha256": "6e7c875d40386854028779fdd1573c45a5ce4a69aba828227fff1bb568f` |
| `experimental/firmware-port/baseline-02.01.01.07.json` | 18 | SHA256-Pin | Gehört zum 02.01.01.07-Patch-Satz; für 02.03.03.13 neu ermitteln (Portierung). Kontext: `"sha256": "ad888fec26ce214427eda038ae70b76cf68155f365c669ed647c1c09bad` |
| `experimental/firmware-port/baseline-02.01.01.07.json` | 23 | SHA256-Pin | Gehört zum 02.01.01.07-Patch-Satz; für 02.03.03.13 neu ermitteln (Portierung). Kontext: `"sha256": "24d0e561287a4b5a87c8ec698c98f60acf2c70511114b36224b4eb92492` |
| `experimental/firmware-port/baseline-02.01.01.07.json` | 28 | SHA256-Pin | Gehört zum 02.01.01.07-Patch-Satz; für 02.03.03.13 neu ermitteln (Portierung). Kontext: `"sha256": "8039081f9c4b01e720d838d4ab6e4515a8979b9a71a33bd4164321c282e` |
| `experimental/firmware-port/baseline-02.01.01.07.json` | 33 | SHA256-Pin | Gehört zum 02.01.01.07-Patch-Satz; für 02.03.03.13 neu ermitteln (Portierung). Kontext: `"sha256": "c26d495c6ea4870cb36a42c0e291db59caf2a9cfaa8782a3befeb766106` |
| `experimental/firmware-port/baseline-02.01.01.07.json` | 38 | SHA256-Pin | Gehört zum 02.01.01.07-Patch-Satz; für 02.03.03.13 neu ermitteln (Portierung). Kontext: `"sha256": "6fed6657e814f010fef87b40c5932d8fa7c5e9c044d7376f25fc2318dae` |
| `experimental/firmware-port/baseline-02.01.01.07.json` | 43 | SHA256-Pin | Gehört zum 02.01.01.07-Patch-Satz; für 02.03.03.13 neu ermitteln (Portierung). Kontext: `"sha256": "9f46cfb7b334e750296f99a7695fd9bec23775cefb65f9d66ac175abae7` |
| `experimental/firmware-port/baseline-02.01.01.07.json` | 48 | SHA256-Pin | Gehört zum 02.01.01.07-Patch-Satz; für 02.03.03.13 neu ermitteln (Portierung). Kontext: `"sha256": "0cb2216fea47b045f5bab2c88f1812560af2ae347e6c0b213788a9b54bc` |
| `experimental/firmware-port/baseline-02.01.01.07.json` | 53 | SHA256-Pin | Gehört zum 02.01.01.07-Patch-Satz; für 02.03.03.13 neu ermitteln (Portierung). Kontext: `"sha256": "14288a4a73c339b039dd89597618ebb05702f90d668b76f74019d5d15ce` |
| `experimental/firmware-port/baseline-02.01.01.07.json` | 58 | SHA256-Pin | Gehört zum 02.01.01.07-Patch-Satz; für 02.03.03.13 neu ermitteln (Portierung). Kontext: `"sha256": "aacfe9a9d847f4be3e4b5a16f7c16cba93a60e25ba371efb77568568b0b` |
| `experimental/firmware-port/baseline-02.01.01.07.json` | 62 | SHA256-Pin | Gehört zum 02.01.01.07-Patch-Satz; für 02.03.03.13 neu ermitteln (Portierung). Kontext: `"sha256": "769d55b96ed4dff486ff5b79f35d61a36d68c444f49f19e44059e2472d4` |
| `experimental/firmware-port/baseline-02.01.01.07.json` | 66 | SHA256-Pin | Gehört zum 02.01.01.07-Patch-Satz; für 02.03.03.13 neu ermitteln (Portierung). Kontext: `"sha256": "b18b3f31b3d378b56b4d01b1863a6b9f0f61211ba1f4ad8e7b672ff5488` |
| `experimental/firmware-port/baseline-02.01.01.07.json` | 70 | SHA256-Pin | Gehört zum 02.01.01.07-Patch-Satz; für 02.03.03.13 neu ermitteln (Portierung). Kontext: `"sha256": "e1ca036e667f1976fb1a1944b6922df8a1c26d5870912dd8273e0fcc3bb` |
| `experimental/firmware-port/baseline-02.01.01.07.json` | 74 | SHA256-Pin | Gehört zum 02.01.01.07-Patch-Satz; für 02.03.03.13 neu ermitteln (Portierung). Kontext: `"sha256": "20375a79e1ee2748f43fd39a62e5a48b23b945ed048dbe52a54d0a1f37b` |
| `experimental/firmware-port/baseline-02.01.01.07.json` | 78 | SHA256-Pin | Gehört zum 02.01.01.07-Patch-Satz; für 02.03.03.13 neu ermitteln (Portierung). Kontext: `"sha256": "6598586ada5e7e9256316c63b801579e65e797d2a77e13ab360f09b3581` |
| `experimental/firmware-port/camera44-manifest.json` | 2 | SHA256-Pin | Gehört zum 02.01.01.07-Patch-Satz; für 02.03.03.13 neu ermitteln (Portierung). Kontext: `"input_sha256": "ad888fec26ce214427eda038ae70b76cf68155f365c669ed647c1` |
| `experimental/firmware-port/camera44-manifest.json` | 3 | SHA256-Pin | Gehört zum 02.01.01.07-Patch-Satz; für 02.03.03.13 neu ermitteln (Portierung). Kontext: `"output_sha256": "ec5633b17de329d5b18c3f8d8de6ebf389391a04193536d976ba` |
| `experimental/firmware-port/dual44-manifest.json` | 3 | SHA256-Pin | Gehört zum 02.01.01.07-Patch-Satz; für 02.03.03.13 neu ermitteln (Portierung). Kontext: `"stock_sha256": "6fed6657e814f010fef87b40c5932d8fa7c5e9c044d7376f25fc2` |
| `experimental/firmware-port/dual44-manifest.json` | 4 | SHA256-Pin | Gehört zum 02.01.01.07-Patch-Satz; für 02.03.03.13 neu ermitteln (Portierung). Kontext: `"payload_sha256": "04b9f093f439c336476f2a34f109b088b05e6c42a4f1baf2f10` |
| `experimental/firmware-port/http44-manifest.json` | 3 | SHA256-Pin | Gehört zum 02.01.01.07-Patch-Satz; für 02.03.03.13 neu ermitteln (Portierung). Kontext: `"baseline_sha256": "8039081f9c4b01e720d838d4ab6e4515a8979b9a71a33bd416` |
| `experimental/firmware-port/http44-manifest.json` | 4 | SHA256-Pin | Gehört zum 02.01.01.07-Patch-Satz; für 02.03.03.13 neu ermitteln (Portierung). Kontext: `"output_sha256": "256bb3b93cb9495cf010efc13379f70873ccf94541ac8e2c2235` |
| `experimental/firmware-port/launcher.sh` | 4 | SHA256-Pin | Gehört zum 02.01.01.07-Patch-Satz; für 02.03.03.13 neu ermitteln (Portierung). Kontext: `EXPECTED=19712b5943b10bb8cb6aab9a4ed498ee962b28538b23c91b7ed003eb9e590` |
| `experimental/firmware-port/mqtt-timer-port-audit.json` | 3 | SHA256-Pin | Gehört zum 02.01.01.07-Patch-Satz; für 02.03.03.13 neu ermitteln (Portierung). Kontext: `"reference_firmware_sha256": "0ea244d0543443d0a87b40cdfd66d8cd57109170` |
| `experimental/firmware-port/mqtt-timer-port-audit.json` | 4 | SHA256-Pin | Gehört zum 02.01.01.07-Patch-Satz; für 02.03.03.13 neu ermitteln (Portierung). Kontext: `"reference_printer_sha256": "c21126e78a63ac9140e7347c56f363d5e513495c5` |
| `experimental/firmware-port/network44-manifest.json` | 3 | SHA256-Pin | Gehört zum 02.01.01.07-Patch-Satz; für 02.03.03.13 neu ermitteln (Portierung). Kontext: `"input_sha256": "256bb3b93cb9495cf010efc13379f70873ccf94541ac8e2c2235b` |
| `experimental/firmware-port/network44-manifest.json` | 4 | SHA256-Pin | Gehört zum 02.01.01.07-Patch-Satz; für 02.03.03.13 neu ermitteln (Portierung). Kontext: `"output_sha256": "19712b5943b10bb8cb6aab9a4ed498ee962b28538b23c91b7ed0` |
| `experimental/firmware-port/patch_zoffset44.py` | 5 | SHA256-Pin | Gehört zum 02.01.01.07-Patch-Satz; für 02.03.03.13 neu ermitteln (Portierung). Kontext: `STOCK='24d0e561287a4b5a87c8ec698c98f60acf2c70511114b36224b4eb9249283ab` |
| `experimental/firmware-port/patch_zoffset44.py` | 6 | Offset/VA | Gehört zum 02.01.01.07-Patch-Satz; für 02.03.03.13 neu ermitteln (Portierung). Kontext: `OFFSET=0x652d0` |
| `experimental/firmware-port/patch_zoffset44.py` | 12 | Offset/VA | Gehört zum 02.01.01.07-Patch-Satz; für 02.03.03.13 neu ermitteln (Portierung). Kontext: `if data[OFFSET:OFFSET+4]!=ORIGINAL:raise ValueError('Z menu branch mis` |
| `experimental/firmware-port/patch_zoffset44.py` | 13 | Offset/VA | Gehört zum 02.01.01.07-Patch-Satz; für 02.03.03.13 neu ermitteln (Portierung). Kontext: `out=bytearray(data);out[OFFSET:OFFSET+4]=PATCHED;return bytes(out)` |
| `experimental/firmware-port/preflight.sh` | 4 | SHA256-Pin | Gehört zum 02.01.01.07-Patch-Satz; für 02.03.03.13 neu ermitteln (Portierung). Kontext: `[ "$(sha256sum /opt/lib/libcolib.so \| awk '{print $1}')" = 14288a4a73c` |
| `experimental/firmware-port/reactor/port-manifest.json` | 2 | SHA256-Pin | Gehört zum 02.01.01.07-Patch-Satz; für 02.03.03.13 neu ermitteln (Portierung). Kontext: `"vendor_sha256": "8039081f9c4b01e720d838d4ab6e4515a8979b9a71a33bd41643` |
| `experimental/firmware-port/reactor/port-manifest.json` | 3 | SHA256-Pin | Gehört zum 02.01.01.07-Patch-Satz; für 02.03.03.13 neu ermitteln (Portierung). Kontext: `"libco_sha256": "14288a4a73c339b039dd89597618ebb05702f90d668b76f74019d` |
