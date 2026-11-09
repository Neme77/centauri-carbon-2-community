# Schicht 2: 0113 im Builder baubar machen — Umsetzungsanleitung

Diese Anleitung setzt auf Schicht 1 auf (Commit `ef75220b` auf Branch
`feat/builder-base-02.01.01.13`): Das Build-Gate prüft jetzt eine **Menge**
qualifizierter Basen (`QUALIFIED_BASES` in
`builder/current/core/firmware_builder.py`). 07 ist drin, 0113 nicht, alles
byte-identisch.

Ziel von Schicht 2: 0113 als zweite baubare Basis ergänzen, ohne den
07-Pfad zu verändern. Alle Schritte sind host-testbar
(`python3 -m unittest discover -s builder/current/tests -p 'test_*.py'`).
Ein echter signierter Build braucht danach noch deine Inputs (Abschnitt
„Harte Blocker").

Bestätigte 0113-Ausgabe-Hashes (auf Hardware und per Emulation belegt):

| Datei | SHA-256 | MD5 | Größe |
|---|---|---|---|
| `elegoo_printer` | `688b7bf8a27762d56b35d9738ea5b0c3d45580b402e122b0f77ee49dab1a8cfd` | `a1cc4bd58769f31dd527dfdf8d0a081d` | 29238216 |
| `ec-eeb001-gui` (Z-Offset) | `668ab8cb490c74eb5bbd769b421f6e4ade4e91fba322e22257496b9645f9ebfe` | — | — |
| `ai_camera` | `90da7634c21b74b3bf943d68129da78668b47f9706620e158283697ab95553a7` | — | — |
| `daemon-000` (Dual Trust) | `83433b14e49d1e8aa9523282ead03618378573f4b66de1ad8f7f3c444b11aada` | — | — |

Stock-0113-Eingaben stehen in `builder/current/base_profiles/02.01.01.13.json`
(`sha256.stock_gui` `8d6cac2a…`, `stock_printer` `f0111e0b…`,
`stock_ai_camera` `e8df8e57…`).

## Grundidee: Ausgabewerte ins Profil, Patch-Pfad pro Basis

Der Builder trägt heute die basis-spezifischen **Ausgabe**-Werte als
Modul-Globals. Für zwei Basen müssen sie aus dem aktiven Profil kommen.
Füge beiden Profilen eine `port`-Sektion hinzu und lies sie im Build.

### Schritt 2.1 — `port`-Sektion in beide Profile

In `base_profiles/02.01.01.07.json` (Werte = die heutigen 07-Konstanten):

```json
"port": {
  "dir": "experimental/firmware-port",
  "printer_patch": "patch44",
  "outputs": {
    "patched_gui": "5f3122852044f6750dbd36aeb2ebcbd0ea8c5e99f48a52c186def1f66c3ec58a",
    "printer_release": {"sha256": "19712b5943b10bb8cb6aab9a4ed498ee962b28538b23c91b7ed003eb9e590745", "md5": "c29e0574fb1693dbb3c516acbf2ad624", "size": 29274740},
    "camera_release": "ec5633b17de329d5b18c3f8d8de6ebf389391a04193536d976ba167022d07b99",
    "native_gui": "db09fa37d378df259427aaac392a341282e08ceb26cd87603542c0dcbcbb91d1",
    "camera_init": "bcabb1fa405b066b06c229c6d8cc6e474a0ccc57e31a1b5b387d3b55945be563"
  },
  "golden": {
    "rootfs": "b9e46320d01d6c3fcc5e35a67c74e81df506afaba754b7a69b5502c78871015b",
    "swu": "d3c0f5287c61cde457938a7c18600e0148873618c5f10f3acff8de1a50f92a8c",
    "swusig": "b6872abd5131fbbdb8075619c56a1e5908d3d601f7698310bb1c698cfd5ff0d4",
    "zip": "564ce9c760763109dd1c876b072357000f58622fa7f04ca8ccbbeacc7be81173",
    "outer": "eeadf35d6310b7f847bc064d744c01fedf918e0b34e100cde37a3a0b2da0f2a4"
  }
}
```

In `base_profiles/02.01.01.13.json` dieselbe Struktur, 0113-Werte:

```json
"port": {
  "dir": "experimental/firmware-port-0113",
  "printer_patch": "apply_patches",
  "outputs": {
    "patched_gui": "668ab8cb490c74eb5bbd769b421f6e4ade4e91fba322e22257496b9645f9ebfe",
    "printer_release": {"sha256": "688b7bf8a27762d56b35d9738ea5b0c3d45580b402e122b0f77ee49dab1a8cfd", "md5": "a1cc4bd58769f31dd527dfdf8d0a081d", "size": 29238216},
    "camera_release": "90da7634c21b74b3bf943d68129da78668b47f9706620e158283697ab95553a7",
    "native_gui": "TODO_NATIVE_DISPLAY_0113",
    "camera_init": "e7537298955e23b7a03cab4c8d7bd241c1dfcc070883cdf51c4889cd255d1bc4"
  },
  "golden": {}
}
```

`load_base_profile()` erweitern: `port` validieren (fester Schlüsselsatz,
jeder Hash 64 hex oder `TODO_…`). Weil die ganze Profildatei in
`QUALIFIED_BASES` hash-gepinnt ist, deckt ein Pin pro Basis alle diese
Werte ab. **Nach jeder Änderung an einer Profildatei den Hash in
`QUALIFIED_BASES` neu setzen**, sonst schlägt `load_base_profile` fehl.

### Schritt 2.2 — Globals durch Profil-Lesen ersetzen

In `firmware_builder.py` `main()` ist das aktive Profil vorhanden
(`profile`, `pins=profile['sha256']`). Leite `port=profile['port']` ab und
reiche die Werte in die Patch-/Audit-Aufrufe:

- `patch_zoffset(gui)` → `patch_zoffset(gui, port['outputs']['patched_gui'], pins['stock_gui'])`; die internen `EXPECTED['stock_gui']`/`EXPECTED['patched_gui']` durch die Parameter ersetzen (Zeilen 360–365).
- Der Printer-Patch wird basis-abhängig (siehe 2.3).
- Kamera: der `reqhash(camera, CAMERA_RELEASE_SHA256, …)` an Zeilen 640/655 → `port['outputs']['camera_release']`.
- Printer: `PRINTER_RELEASE_SHA256`/`_MD5`/`_SIZE` an Zeilen 459–461, 510, 641, 656, 723 → `port['outputs']['printer_release']`.
- Natives Display: `display.GUI_SHA256` (Zeilen 639/654) → `port['outputs']['native_gui']` durchreichen (siehe 3a).
- `GOLDEN` (Zeile 111) + `golden_report()` (226) → `port['golden']`.
- `PORT_DIR`/`PORT_HASHES` (492/494) → aus `port['dir']` ableiten; `validate_port_inputs()`/`port_module()` den Pfad übergeben.

`EXPECTED` behält nur basis-unabhängige Einträge (sshd, aes_key,
Patcher-Tools, `daemon_dual` — daemon ist byte-identisch zu 07, bleibt).

### Schritt 2.3 — 0113-Printer über `apply_patches.py`

Der 07-Printer-Patch ist zweistufig (`patch_http44`→`patch_network44`,
Zwischen-Hash). Auf 0113 läuft das **nicht** — die 07-`input_sha256`-Pins
lehnen die 0113-Stock-Binary ab. 0113 nutzt stattdessen den
K1-Restructure in `experimental/firmware-port-0113/apply_patches.py`
(bestätigt: erzeugt exakt `688b7bf8…`).

Mach den Printer-Patch über `port['printer_patch']` fallunterscheidbar:
- `"patch44"` → heutiger `patch_printer44(printer)`.
- `"apply_patches"` → neue Funktion, die
  `experimental/firmware-port-0113/apply_patches.py` auf die extrahierten
  Stock-GUI/Printer/Kamera anwendet und die drei gepatchten Ausgaben
  zurückschreibt. `apply_patches.py` verarbeitet alle drei Binaries in
  einem Lauf; rufe es einmal statt der separaten
  zoffset/printer/camera-Patches auf, wenn die Basis 0113 ist.
- **Wichtig:** Vor dem Patchen Stock-Input-SHA gegen die Profil-Pins
  prüfen (`stock_gui`/`stock_printer`/`stock_ai_camera`); `apply_patches.py`
  prüft heute nur Vorher-Bytes, nicht den vollen Datei-Hash. Nach dem
  Patchen gegen `port['outputs']` prüfen.

Die Port-Dateien von `firmware-port-0113/` in `PORT_HASHES`-Manier pinnen
(wie der 07-`PORT_HASHES`-Block), damit niemand das Patch-Skript
unbemerkt ändert.

### Schritt 2.4 — Komponenten re-pinnen

- **Kamera-Runtime** (`core/camera_component.py:13`): `INIT_SHA` ist der
  07-Stock-`etc/init.d/printer`. Mach ihn basis-abhängig
  (`port['outputs']['camera_init']`); 0113-Wert
  `e7537298955e23b7a03cab4c8d7bd241c1dfcc070883cdf51c4889cd255d1bc4`.
  Danach prüfen, dass die Regex `^[ \t]*ai_camera &$` in `patched_init()`
  im 0113-`printer`-Text noch greift (ist Text-basiert, hält
  voraussichtlich).
- **CC2-Control** (`cc2-control/installer/compatibility.json`):
  `printer_sha256` auf `688b7bf8…` und `firmware_version_file_sha256` auf
  den 0113-Wert setzen; `daemon_sha256` bleibt (byte-identisch). Für eine
  eigene Kompatibilitätsfamilie (z. B. „4.5") ggf. `vendor_base` anpassen —
  das ist eine Maintainer-Entscheidung.

### Schritt 2.5 — Host-Tests ergänzen

- `test_base_profiles.py`: `port`-Schema prüfen (fester Schlüsselsatz,
  Hashformate), für 07 und 0113.
- Neuer Test: `apply_patches.py` erzeugt aus den Stock-0113-Inputs genau
  die `port.outputs`-Hashes (nur lauffähig, wo die Stock-Inputs lokal
  vorliegen — sonst als `skipUnless` markieren).
- Neuer Test: keine basis-spezifische Ausgabe-Konstante bleibt als
  Modul-Global in `firmware_builder.py` (statischer Namens-Check).
- Bestehende Tests müssen grün bleiben; die aus dem Code entfernten
  07-Konstanten müssen zeichengleich in der 07-`port`-Sektion stehen.

## Schicht 3: freischalten (erst nach 2 + den Blockern)

Wenn alle `port.outputs` gesetzt sind (inkl. `native_gui` aus 3a) und die
Kernelmodule stehen (3b): im 0113-Profil `ported_patch_set: true`, 0113 in
`QUALIFIED_BASES` eintragen (mit dem dann gültigen Profil-Hash),
`test_base_profiles.py` erweitern.

## Harte Blocker (nur mit deinen Inputs lösbar)

1. **Natives Display (3a).** Das native Display überschreibt im Build die
   Z-Offset-GUI (`firmware_builder.py:634`, Prüfung :639). Für 0113 müssen
   `experimental/native-display/patch-plan.json` (`base_sha256`,
   ELF-Offsets, `before_sha256`, `output_sha256`) und
   `baseline-pins.txt` (Bild-Recolor) gegen die 0113-GUI (`668ab8cb…`) und
   die **0113-Stock-Theme-Bilder** neu abgeleitet werden. Die Bilder
   kommen aus dem entschlüsselten 0113-Stock-Rootfs → brauchen Stock-OTA +
   AES-Key. Ergebnis ist der neue `native_gui`-Hash
   (`TODO_NATIVE_DISPLAY_0113`).
2. **USB-Ethernet-Kernelmodule (3b).** `cc2_ue300_*.ko` sind gegen die
   07-Kernel-ABI gebaut; der 0113-Kernel weicht ab (gleicher
   Versionsstring 5.4.61, anderer Hash). Neubau über
   `prepare_usb_runtime.py` (`arm-linux-gnueabihf-gcc`) **braucht
   0113-Kernel-Header/-Quellen**. Danach `connections_component.py`
   `NATIVE`/`BINARY_PINS` und den `aic8800_fdrv.ko`-Pin auf 0113 setzen.
3. **Signierung (4).** Der Builder verlangt den privaten Community-Key,
   der gegen einen gepinnten Public-Hash geprüft wird. Kein
   unsigniertes/selbstsigniertes echtes Build möglich (`--preflight-only`
   greift erst nach der Key-Prüfung). Nur du.

## Minimaler Satz Operator-Inputs für einen echten 0113-Build

Privater Community-Key; `cc2_aes_key_v1.bin`; Stock-OTA unter
`original_firmware/`; die Vendor-Runtimes (`sshd`, `dual_verify.bin`,
`connections/runtime.tar.gz`, vorbereitete reactor/camera/cc2-control);
Host-Tools `mksquashfs`/`unsquashfs`/`openssl`/`cpio`. Der secret-freie
Nachweis vorab ist allein die Host-Test-Suite.
