# Relocation analysis: ELEGOO base 02.01.01.13

This directory holds the relocation analysis for porting the existing
`experimental/firmware-port/` community patches from ELEGOO base
**02.01.01.07** to **02.01.01.13**. It is analysis only. No ported byte
patches, manifests or vendor binaries are committed here yet; those are written
and hardware-tested by a maintainer, following `AGENTS.md` and
`UPDATE_PLAN_02.01.01.13.md`.

## Status: Drucker-Patch-Verifikation

**Before-Fenster verifiziert; After-Seite, Cross-Jumps und Payload-Ziele noch
nicht verifiziert.**

- Geprüfte `before: MATCH`-Stellen: 19
- Bestätigter Adress-Shift: +0x7790
- Before-Fenster byte-identisch: 19/19
- After-Seite / relative Sprungziele: noch nicht verifiziert
- Payload-Ziele, insbesondere `0xcd77e8`: noch nicht verifiziert
- DIFF-Blöcke: separat behandelt, nicht Teil der regulären Verifizierung
- Emulations- und Hardware-Test: ausstehend

Die 19 MATCH-Stellen bestätigen ausschließlich, dass die ursprünglichen
Before-Bytes an der um +0x7790 verschobenen Adresse unverändert sind. Sie
belegen nicht die Korrektheit der After-Bytes, relativen Sprungziele,
Payload-Referenzen oder der Funktionslogik nach dem Patch.

GUI und Kamera sind separat zugeordnet (eigene Abschnitte unten);
`device_status_offset` (`0xbbc`) ist statisch verifiziert. Für alle gilt: der
Hardware-Test steht noch aus.

### Ausgeschlossene DIFF-Blöcke

| Alte Adresse | Kontext |
|---|---|
| 0x812128 | set_mode-Block |
| 0x812158 | network-manifest / HTTP-DIFF-Kontext; Teil des set_mode-Umbaus |
| 0x837168 | AsyncMQTTConnector-Konstruktor |
| 0x8371a0 | AsyncMQTTConnector-Konstruktor |
| 0x84f940 | handle_file_upload-Block |
| 0x851754 | check_token |
| 0x851924 | check_token |

Die Detailtabellen zur Before-Fenster-Verifikation und zu GUI/Kamera stehen
unterhalb dieses Statusblocks.

## How this was produced

The vendor binaries are not redistributed. Analysis ran locally against:
- the captured official 02.01.01.13 package (`builder/current/original_firmware/`,
  verified: stock RSA signatures valid, embedded SHA-256 match);
- the stock 02.01.01.07 `elegoo_printer` and `ai_camera`, reconstructed by
  reversing the published `*44-manifest.json` patch sets over the V4.4.3 release
  binary and confirmed against their pinned hashes
  (`elegoo_printer` → `8039081f…`, matching `PRINTER_STOCK_SHA256`).

Both binaries are **not stripped**, so every patch site maps to a named
function through the ELF symbol table. All offsets were read from the ELF
sections and `PT_LOAD` segments; nothing was disassembled or modified.

## Result: the patched binaries are relocated, not restructured

Every function that the existing patches touch exists in the 02.01.01.13 build
with an **identical byte size**, shifted by a single constant per binary:

| Binary | stock base | 02.01.01.13 | uniform shift | functions touched |
|---|---|---|---|---|
| `opt/bin/elegoo_printer` | `8039081f…` | `f0111e0b…` | **+0x7790** | all HTTP + network sites |
| `opt/bin/ai_camera` | stock recon | `e8df8e57…` | **+0x90b0** | `znp::AICamera<UvcCamera>::LocalRtcMonitor` |

A constant shift at identical function sizes means the compiler placed the same
functions further along in `.text` (earlier code grew) but did **not** change
their internal layout. That is the good case for a port: the internal branch
and instruction structure is expected to be the same, so the existing
before/after byte windows are the right starting point.

### `elegoo_printer` — HTTP upload (`http44-manifest.json`, 11 sites)

Every site maps and every function keeps its size (shift +0x7790):

| stock VA | function |
|---|---|
| 0x812128, 0x812494 | `communication::NetworkManager::set_mode(NetworkMode)` |
| 0x81f1b4 | `communication::LanStrategy::stop()` |
| 0x84ec20, 0x84f940, 0x850000, 0x850238, 0x8509a8, 0x8509c4, 0x850eb4 | `communication::HTTPServer::handle_file_upload…` |
| 0x852500 | `communication::HTTPServer::handle_upload_…` |
| 0x850eb4 | `communication::HTTPServer::handle_file_ca…` |

### `elegoo_printer` — network/LAN-WAN (`network44-manifest.json`)

Functions touched, all present at identical size (shift +0x7790):
`LanStrategy::start`, `NetworkManager::{set_mode, report_message, stop}`,
`api_module::DeviceStatus::post_change_message`, `HTTPServer::check_token`
(two overloads), `AsyncMQTTConnector` constructor.
One manifest entry (`0xcd77e8`) is a **`.data` member offset**
(`device_status_offset`), not a code site, and is handled separately from the
relocation. The tiny entries `0xa4`/`0xa8` are ELF program-header `p_filesz` /
`p_memsz` fields, unrelated to code relocation.

#### `device_status_offset` (`0xbbc`) — statically validated (2026-11-08)

Status: **statisch validiert (Struktur-Layout identisch bei `0xbbc`)**;
hardware test still pending, like every site here.

`device_status_offset` is the offset of the machine-status member inside the
`api_module::DeviceStatus` singleton (the value `test_http44_emulation.py`
writes at `object + 0xbbc`). It is a struct field offset, not a code address, so
it is validated by confirming the member is still read at `[this, #0xbbc]` in
the new build — not by any function-address shift.

Both builds were scanned for immediate `[reg, #0xbbc]` loads. The **same**
`DeviceStatus` core functions read the member at `0xbbc` in 02.01.01.07 and in
02.01.01.13, with the same `ldr rX,[this,#0xbbc]` form: `set_status` (both
overloads), `update_device_status`, `on_sub_status_changed`, `set_ui_ready`,
`is_cloud_slicer_status`, `handle_reset_ai_foreign_object_region_exclusion`
(16 accesses in each build). The member offset is therefore unchanged.

Not evidence for this: `DeviceStatus::get_post_id()` loads the **static** member
`post_message_id_` from a global address (`0xcece4c` → `0xcd9e54`) via
`movw`/`movt` in both builds. A static member is always global (not at a `this`
offset), so it neither proves nor disproves the `0xbbc` layout; its global move
is an ordinary `.bss` relocation. `DeviceStatusWorker` functions are a different
class and shift `+0x2530`; that shift is also unrelated to `device_status_offset`.

#### `before`-window check for the simple sites (2026-11-08)

Read-only pass over both builds: for every `before: MATCH` site in
`http44`/`network44`, the `before` bytes (the pre-image the patch overwrites)
are **byte-identical at exactly `alt + 0x7790`** in 02.01.01.13. This confirms
the patch **site** is correctly located and its pre-image is intact — maintainer
step 1 only. **It does NOT verify the `after` side.**

**Scope and limit.** `before: MATCH` ≠ patch portable. Several of these sites'
`after` bytes branch into the appended payload block `0xcd77e8` (which does not
yet exist in stock 02.01.01.13 and must be re-placed) or into other functions
that shifted (`set_status`, `report_message`, cross-jumps inside
`handle_file_upload`). Those targets must be recomputed separately; the
before-window match says nothing about them.

19 sites, all `before: MATCH`, shift `+0x7790` confirmed per site:

| Caller | Alt | Neu (= Alt+0x7790) | before |
|---|---|---|---|
| `NetworkManager::stop` | 0x811c0c | 0x81939c | `0200a0e1` |
| `NetworkManager::stop` | 0x811c10 | 0x8193a0 | `33ff2fe1` |
| `NetworkManager::report_message` | 0x811c70 | 0x819400 | `33ff2fe1` |
| `NetworkManager::set_mode` | 0x812120 | 0x8198b0 | `0200a0e1` |
| `NetworkManager::set_mode` | 0x812124 | 0x8198b4 | `33ff2fe1` |
| `NetworkManager::set_mode` | 0x812494 | 0x819c24 | `00f020e3` |
| `LanStrategy::start` | 0x81f028 | 0x8267b8 | `54304be2` |
| `LanStrategy::stop` | 0x81f1b4 | 0x826944 | `ff0100eb` |
| `HTTPServer::handle_file_upload` | 0x84ec20 | 0x8563b0 | `14301be50b0053e3` |
| `HTTPServer::handle_file_upload` | 0x850000 | 0x857790 | `b1fa00eb` |
| `HTTPServer::handle_file_upload` | 0x850238 | 0x8579c8 | `12f900eb` |
| `HTTPServer::handle_file_upload` | 0x8509a8 | 0x858138 | `36f700eb` |
| `HTTPServer::handle_file_upload` | 0x8509c4 | 0x858154 | `0ef900eb` |
| `HTTPServer::handle_file_cancel` | 0x850eb4 | 0x858644 | `3c311be5` |
| `HTTPServer::handle_upload_timeout` | 0x852500 | 0x859c90 | `3ff200eb` |
| `DeviceStatus::post_change_message` | 0x88db48 | 0x8952d8 | `000053e30130a0030030a013` |
| `DeviceStatus::post_change_message` | 0x88dd54 | 0x8954e4 | `b10ffeeb` |
| `DeviceStatus::post_change_message` | 0x88dd70 | 0x895500 | `9d0000ea` |
| `DeviceStatus::post_change_message` | 0x88dfc4 | 0x895754 | `150ffeeb` |

Excluded, `before: DIFF` (recompiled, maintainer disassembly): `0x812128`
(`set_mode` out-of-line block), `0x84f940` (`handle_file_upload` block),
`0x837168`/`0x8371a0` (`AsyncMQTTConnector`), `0x851754`/`0x851924`
(`check_token`). `0x812158` is UNCLEAR: its `network44` `before` is taken against
the HTTP-patched binary, not stock, and it falls inside the `set_mode` rework, so
it is part of that block too.

#### `after`-side classification of the 19 sites (2026-11-08)

Read-only decode of each site's `after` bytes. For in-function branches, the
16-byte window at `target + 0x7790` in the new build was compared to the old
target: identical means the relative branch still lands on the same instruction.

**After position-independent — portable without `after` recomputation (7)**
(before-window must still hold; global emulation + hardware test still apply):

| Stelle | Caller | After-Typ |
|---|---|---|
| 0x811c0c | `NetworkManager::stop` | data (`ldr [r11,#…]`) |
| 0x812120 | `NetworkManager::set_mode` | data (`ldr [r11,#…]`) |
| 0x81f1b4 | `LanStrategy::stop` | NOP |
| 0x84ec20 | `HTTPServer::handle_file_upload` | data (status compare) |
| 0x850238 | `HTTPServer::handle_file_upload` | in-function branch → 0x84f990, target window identical |
| 0x8509a8 | `HTTPServer::handle_file_upload` | in-function branch → 0x84f990, target window identical |
| 0x88db48 | `DeviceStatus::post_change_message` | data (`cmp`/`mov`) |

**After must be recomputed — not portable / after open (9)**:

| Stelle | Caller | After-Typ | Ziel |
|---|---|---|---|
| 0x811c10 | `NetworkManager::stop` | payload ref | 0xcd7844 |
| 0x811c70 | `NetworkManager::report_message` | payload ref | 0xcd788c |
| 0x812124 | `NetworkManager::set_mode` | payload ref | 0xcd7844 |
| 0x81f028 | `LanStrategy::start` | payload ref | 0xcd7824 |
| 0x88dd54 | `DeviceStatus::post_change_message` | payload ref | 0xcd78cc |
| 0x88dd70 | `DeviceStatus::post_change_message` | payload ref | 0xcd78f8 |
| 0x88dfc4 | `DeviceStatus::post_change_message` | payload ref | 0xcd78e8 |
| 0x850eb4 | `HTTPServer::handle_file_cancel` | cross-function branch | 0x84f9d0 (handle_file_upload) |
| 0x852500 | `HTTPServer::handle_upload_timeout` | cross-function branch | 0x84f9b0 (handle_file_upload) |

**Eingegrenzt (zuvor UNKLAR), Emulation ausstehend (3)**: Die 16-Byte-Fenster
dieser In-Funktions-Branches wichen nur durch **relozierte Operanden** ab
(verschobene `bl`-Callees, `movw`/`movt`-Globale), nicht durch geänderte
Code-Struktur. Opcode-Vergleich zeigt: das Ziel liegt bei `+0x7790`, die nicht
relozierten Opcodes sind byte-gleich und an gleicher relativer Position. Damit
bleiben Patch-Stelle und Ziel bei `+0x7790`, die relative Distanz erhalten, und
die `after`-Bytes dieser 3 Stellen sind unverändert gültig. Keine Neuberechnung
nötig; finale Bestätigung per Emulationstest. Dies ist ein Opcode-Vergleich,
keine vollständige Disassemblierung.

| Stelle | Caller | Ziel alt → neu | Beleg (nicht relozierte Opcodes) |
|---|---|---|---|
| 0x812494 | `NetworkManager::set_mode` | 0x81212c → 0x8198bc (+0x7790) | `e12fff33` / `e1a04000` / `e24b00f8` identisch; nur `bl spdlog` + `movw`/`movt` reloziert |
| 0x850000 | `HTTPServer::handle_file_upload` | 0x84f970 → 0x857100 (+0x7790) | `e50b32e0` / `e1a03000` identisch; nur `movw`/`movt` + `bl char_traits::length` reloziert |
| 0x8509c4 | `HTTPServer::handle_file_upload` | 0x84f9b0 → 0x857140 (+0x7790) | `e893000e` / `e1a00004` / `e24b3e4e` identisch; nur `bl logger::log` reloziert |

Count: position-independent 7, recompute 9, unclear 3. The payload references all
point into the `0xcd77e8` block, which must be placed first; then those targets
are recomputed against its new position.

### Payload placement

**Befund: Die bisherige Platzierungsmethode für den Payload-Block ist im neuen Build nicht mehr möglich.**

Im alten Build 02.01.01.07 lag der 500-Byte-Payload an VA `0xcd77e8`, direkt hinter dem Ende des ausführbaren Segments `LOAD[3]`. Dort war eine ausreichend große Null-Lücke vorhanden; `p_filesz` und `p_memsz` wurden entsprechend um 500 Bytes erhöht.

Im neuen Build 02.01.01.13 beträgt die freie Lücke hinter `LOAD[3]` nur noch 84 Bytes (`0x54`). Der nächste Datenbereich, `LOAD[4]`, beginnt bei Datei-Offset `0xcb5000`. Eine Platzierung von 500 Bytes an dieser Stelle würde daher `LOAD[4]` überschreiben.

Eine Suche nach einer ausreichend großen Null-Lücke innerhalb des ausführbaren Segments ergab keinen geeigneten Kandidaten. Der größte gefundene Null-Lauf beträgt 283 Bytes und liegt am Anfang des Segments; es gibt somit keine Drop-in-Position für den Payload-Block.

#### Betroffene ELF-Struktur

| Feld | Wert |
|---|---|
| Betroffenes Segment | `LOAD[3]`, Flags `R-X` |
| Programmheader-Offset | `0x94` |
| `p_offset` / `p_vaddr` | `0x0` / `0x10000` |
| Aktuelles `p_filesz` | `0xcb4fac` |
| Aktuelles `p_memsz` | `0xcb4fac` |
| Segment-Ende, virtuelle Adresse | `0xcc4fac` |
| Segment-Ende, Datei-Offset | `0xcb4fac` |
| Nächster Datenbereich | `LOAD[4]` bei Datei-Offset `0xcb5000` |
| Freie Lücke | `0x54` = 84 Bytes |
| `p_align` | `0x10000` |

#### Erforderliche strukturelle Anpassung

Um 500 Bytes hinter dem aktuellen Ende von `LOAD[3]` unterzubringen, müsste der Bereich ab Datei-Offset `0xcb4fac` frei sein. Dieser Bereich gehört jedoch bereits zu `LOAD[4]`.

Eine saubere Platzierung erfordert daher eine strukturelle Anpassung des ELF-Layouts:

- Verschiebung von `LOAD[4]` (`p_offset` aktuell `0xcb5000`)
- Verschiebung von `DYNAMIC` bei `0xcb51f8`
- Verschiebung von `GNU_RELRO` / Segmenttyp 7 bei `0xcb5000`
- Anpassung von `e_shoff` sowie aller Section-Offsets ab `0xcb5000`
- Anpassung von `p_filesz` und `p_memsz` von `LOAD[3]`
- Wahrung der Kongruenz `p_offset ≡ p_vaddr (mod 0x10000)`

Aufgrund von `p_align = 0x10000` wäre ein verschiebungsfreundliches Delta praktisch `+0x10000`, sofern die virtuelle Adresse von `LOAD[4]` unverändert bleiben soll. Diese Arbeit sollte nicht manuell per Byte-Patch erfolgen, sondern über einen geeigneten Linker- bzw. `objcopy`-basierten Toolchain-Schritt.

#### Bedingter Kandidat

Nur falls die erforderliche Segment-Umstrukturierung durchgeführt wird, wäre `VA 0xcc4fac` ein möglicher Kandidat für den Payload-Beginn:

- Direkt hinter dem aktuellen Ende von `LOAD[3]`
- 4-Byte-ausgerichtet, passend für ARM
- Entfernung zu den Aufrufern im Bereich `0x81xxxx` bis `0x89xxxx` bleibt deutlich unter 1 MB und damit innerhalb des ARM-Branch-Bereichs von ±32 MB
- Nach der Verschiebung muss ein ausreichender Puffer bleiben, damit der Payload nicht in `LOAD[4]` hineinreicht

Diese Adresse ist ausdrücklich **nicht final**. Sie ist nur unter der Bedingung gültig, dass die beschriebene Segment-Verschiebung korrekt und verifiziert durchgeführt wurde.

#### Payload-Referenzen

Die bisherigen Payload-Ziele lassen sich in zwei Gruppen trennen.

**Gruppe A — Referenzen in den Payload-Block.** Diese Referenzen ändern sich, sobald die neue Payload-Basis feststeht:

| Offset im Payload | Alte Ziel-VA | Aufrufer |
|---|---:|---|
| `+0x00` | `0xcd77e8` | `0x812158` (DIFF) |
| `+0x3c` | `0xcd7824` | `0x81f028` |
| `+0x5c` | `0xcd7844` | `0x812124`, `0x811c10` |
| `+0xa4` | `0xcd788c` | `0x811c70` |
| `+0xe4` | `0xcd78cc` | `0x88dd54` |
| `+0x100` | `0xcd78e8` | `0x88dfc4` |
| `+0x110` | `0xcd78f8` | `0x88dd70` |
| `+0x124` | `0xcd790c` | `0x851754`, `0x851924` (DIFF) |
| `+0x1c4` | `0xcd79ac` | `0x837168` (DIFF) |
| `+0x1dc` | `0xcd79c4` | `0x8371a0` (DIFF) |

```text
neue_Zieladresse = Payload_Basis_neu + Offset_im_Payload
```

**Gruppe B — Branches im Payload auf Vendor-Funktionen.** Diese Ziele müssen doppelt neu berechnet werden:

- Die Quelladresse ändert sich, weil der Payload-Block selbst umzieht.
- Das jeweilige Vendor-Ziel ist ebenfalls verschoben.
- Ein pauschaler Shift von `+0x7790` ist hier nicht zulässig.

Beispielsweise liegt `NetworkManager::get_instance` bei `0x25606c` in einem anderen Bereich mit eigenem Shift. Jede Vendor-Zielfunktion muss daher einzeln verifiziert und ihre tatsächliche neue Adresse bestimmt werden.

#### Gruppe-B-Ziele (verifiziert, nur Zieladressen)

Lesende Verifizierung jedes Payload-internen Branches gegen den neuen Build.
Dokumentiert sind ausschließlich die belegten Vendor-Zieladressen. **Keine
Branch-Bytes, keine `imm24`-Werte, keine finalen Sprungadressen** — die hängen
zusätzlich von der finalen Payload-Basis ab.

| Payload-interner Branch | Quell-VA im Payload | Vendor-Zielfunktion | Alte Ziel-VA | Neue Ziel-VA | Tatsächlicher Shift | Shift bestätigt? | Referenzform | Verifizierungsgrundlage | Status |
|---|---|---|---|---|---|---|---|---|---|
| 1 | `0xcd781c` | `LanStrategy::start()` | `0x81ef10` | `0x8266a0` | `+0x7790` | Ja | `bl` | Einsprung Off 0 | Ziel verifiziert |
| 2 | `0xcd7820` | `NetworkManager::set_mode` +0x68c | `0x812498` | `0x819c28` | `+0x7790` | Ja | `b` | Fenster identisch | Ziel verifiziert |
| 3 | `0xcd7828` | `NetworkManager::get_instance()` | `0x25606c` | `0x25859c` | `+0x2530` | Ja | `bl` | Einsprung Off 0 | Ziel verifiziert |
| 4 | `0xcd7840` | `LanStrategy::start()` +0x11c | `0x81f02c` | `0x8267bc` | `+0x7790` | **bestätigt** | `b` | Vollständige Capstone-Disassemblierung der ganzen Funktion: 161/161 Instruktionen strukturell identisch, Patch-Stelle an Index 71 in beiden Builds `mov r0,r3` | bestätigt (2026-11-08) |
| 5 | `0xcd787c` | `LanStrategy::stop_mqtt_client()` | `0x81fb38` | `0x8272c8` | `+0x7790` | Ja | `bl` | Einsprung Off 0 | Ziel verifiziert |
| 6 | `0xcd78c4` | `LanStrategy::report_message()` | `0x81f24c` | `0x8269dc` | `+0x7790` | Ja | `bl` | Einsprung Off 0 | Ziel verifiziert |
| 7 | `0xcd78e4` | `LanStrategy::report_message()` | `0x81f24c` | `0x8269dc` | `+0x7790` | Ja | `b` | Einsprung Off 0 | Ziel verifiziert |
| 8 | `0xcd78f4` | `WanStrategy::report_message()` | `0x81d9b8` | `0x825148` | `+0x7790` | Ja | `b` | Einsprung Off 0 | Ziel verifiziert |
| 9 | `0xcd78f8` | `NetworkManager::get_instance()` | `0x25606c` | `0x25859c` | `+0x2530` | Ja | `bl` | Einsprung Off 0 | Ziel verifiziert |
| 10 | `0xcd78fc` | `NetworkManager::get_mode()` | `0x8124a0` | `0x819c30` | `+0x7790` | Ja | `bl` | Einsprung Off 0 | Ziel verifiziert |
| 11 | `0xcd7908` | `DeviceStatus::post_change_message` +0x57c | `0x88dfec` | `0x89577c` | `+0x7790` | Ja | `b` | Fenster identisch | Ziel verifiziert |
| 12 | `0xcd7914` | `strcmp` | `0xc0090` | `0xbffb8` | `-0xd8` | Ja | direkter Branch auf PLT-Stub, Sprung über GOT | `.rel.plt`, Name eindeutig | Ziel verifiziert |
| 13 | `0xcd7924` | `DeviceStatus::getInstance()` | `0x87ab00` | `0x882290` | `+0x7790` | Ja | `bl` | Einsprung Off 0 | Ziel verifiziert |
| 14 | `0xcd7958` | `std::string::append` | `0xbf7f0` | `0xbf70c` | `-0xe4` | Ja | direkter Branch auf PLT-Stub, Sprung über GOT | `.rel.plt`, Name eindeutig | Ziel verifiziert |
| 15 | `0xcd7964` | `strcmp` | `0xc0090` | `0xbffb8` | `-0xd8` | Ja | direkter Branch auf PLT-Stub, Sprung über GOT | `.rel.plt`, Name eindeutig | Ziel verifiziert |
| 16 | `0xcd79c0` | `std::string` ctor | `0xbf2c8` | `0xbf1e4` | `-0xe4` | Ja | direkter Branch auf PLT-Stub, Sprung über GOT | `.rel.plt`, Name eindeutig | Ziel verifiziert |
| 17 | `0xcd79d8` | `std::string` ctor | `0xbf2c8` | `0xbf1e4` | `-0xe4` | Ja | direkter Branch auf PLT-Stub, Sprung über GOT | `.rel.plt`, Name eindeutig | Ziel verifiziert |

(17 Branches, 13 eindeutige Vendor-Ziele.)

##### Gruppe B — Vendor-Ziele im Payload

Alle 13 Vendor-Ziele sind adressseitig belegt: 13 verifiziert.

- `LanStrategy::start + 0x11c` (`0xcd7840`) → neu `0x8267bc` (`+0x7790`). **Bestätigt
  (2026-11-08)** durch vollständige Capstone-Disassemblierung der ganzen Funktion
  in beiden Builds: 161/161 Instruktionen strukturell identisch (Mnemonics +
  normierte Operanden; nur Branch-Ziele und `movw`/`movt` unterscheiden sich
  relozierungsbedingt). Patch-Stelle an Instruktions-Index 71 in beiden Funktionen,
  dort jeweils `mov r0,r3` (alt `0x81f02c`, neu `0x8267bc`), mit übereinstimmendem
  Kontrollfluss davor/danach. Kein struktureller Hinweis mehr, sondern
  vollständiger Funktionsvergleich.

Es liegen vier unterschiedliche Shifts vor. Ein pauschaler Shift von `+0x7790` wäre bei vier Zielen falsch:

| Shift | Betroffene Ziele |
|---:|---|
| `+0x7790` | 9 Ziele, Netzwerk-/HTTP-Code |
| `+0x2530` | `NetworkManager::get_instance`, anderer `.text`-Bereich |
| `-0xd8` | `strcmp`, neu gebauter PLT |
| `-0xe4` | Zwei `std::string`-PLT-Stubs |

Alle verifizierten Referenzen sind direkte Branches (`b`/`bl`). Bei den PLT-Fällen handelt es sich um direkte Branches auf einen PLT-Stub, der anschließend über die GOT springt.

##### ARM-Reichweite

Bei der bedingten Payload-Basis `0xcc4fac` beträgt die Entfernung zum entferntesten Ziel, `0x25859c`, etwa 10,9 MB. Alle bisher verifizierten Ziele bleiben damit deutlich innerhalb des ARM-Branch-Bereichs von ±32 MB.

Diese Reichweitenprüfung bleibt vorläufig, solange die finale Payload-Basis nicht feststeht. Nach der endgültigen Platzierung ist sie für jede Gruppe-B-Referenz anhand der tatsächlichen Quell- und Zieladresse erneut zu bestätigen.

##### Abhängigkeit der Branch-Bytes

Die endgültigen Branch-Bytes im Payload hängen von zwei Werten ab:

1. Der finalen Payload-Basis.
2. Der jeweiligen Quelladresse im verschobenen Payload.

Erst wenn beide feststehen, können die `imm24`-Werte der `b`-/`bl`-Instruktionen berechnet werden. Bis dahin bleibt der Status der Branch-Bytes ausdrücklich offen.

#### Alternative Platzierungs-Kandidaten (lesende Analyse)

ELF-Layout-Befunde des neuen Builds:

- Programmheader-Tabelle `0x34`–`0x154`; `INTERP` folgt direkt bei `0x154` →
  **kein freier Programmheader-Slot** (ein weiterer bräuchte bis `0x174`).
- `GNU_STACK` (Programmheader `0x134`) ist **vollständig Null** → umwidmbar.
- VA-Lücke zwischen `LOAD[3]`-Ende (`0xcc4fac`) und `LOAD[4]` (`0xcd5000`):
  `0x10054`; die zugehörige **Datei**-Lücke nur `0x54` (84 B).
- Nach `LOAD[4]`-Datenende (`0xcbbe78`) bis `e_shoff` (`0x1bd1f18`): `~0xf160a0`
  (~15,8 MB) **nicht geladene** Sektionsdaten (`.symtab`/`.strtab`/…).
- `p_align = 0x10000`; Kongruenz `p_offset ≡ p_vaddr (mod 0x10000)` muss bleiben.

| # | Kandidat | VA | Vorteile | Nachteile / Konflikte | Alignment / Abstand |
|---|---|---|---|---|---|
| K1 | `LOAD[4]`+`DYNAMIC`+`RELRO`+Sektionen um `+0x10000` verschieben, `LOAD[3]` verlängern | `0xcc4fac` | Payload direkt hinter `.text`, Standard-Exec-Segment, Stack bleibt geschützt, nahe den Aufrufern | große mechanische Datei-OP: `e_shoff` und alle Sektion-Offsets ab `0xcb5000` anpassen; nur per Linker/`objcopy` | `+0x10000` ist Vielfaches von `p_align`, Kongruenz gewahrt |
| K2 | `GNU_STACK` → `PT_LOAD` (R-X) umwidmen, Payload am Dateiende/Cave anhängen, frische VA | frei wählbar in Reichweite | additiv, keine Verschiebung bestehender Segmente/Sektionen, nutzt den Null-Slot | Wegfall von `GNU_STACK` kann **ausführbaren Stack** bedeuten (Sicherheitsregression); nicht-standard; Vendor-Loader könnte geänderte Segmenttabelle ablehnen | neue VA 4-Byte-ausgerichtet, Kongruenz zu Datei-Offset wahren |
| K3 | Code-Caves (Null-Läufe) im Exec-Segment | mehrere | kein Struktureingriff | größter Null-Lauf nur **283 B** < 500; Aufteilung mit Verbindungs-Branches nötig, fragil | — |
| K4 | 84-Byte-Datei-Lücke / reine VA-Lücke | `0xcc4fac` | — | Datei-Lücke zu klein (84 B); VA-Lücke nicht datei-gestützt (nur `.bss`, nicht ausführbar) | nicht nutzbar |

Empfehlung (vorläufig, lesend): **K1** ist der sauberste Weg (Standard-Exec-Segment,
Stack bleibt nicht-ausführbar), erfordert aber einen Toolchain-/Linker-Schritt.
**K2** ist die additive Rückfalloption, bringt jedoch das Exec-Stack-Risiko und
ist nicht-standard. K3/K4 sind untauglich bzw. nur Notlösung. Keine Adresse ist
final; die Wahl und die Durchführung bleiben Maintainer-/Toolchain-Arbeit.

#### Offene Punkte

- Finale Payload-Basis: offen
- Branch-Bytes für alle Gruppe-B-Referenzen: offen, abhängig von finaler Payload-Basis und Quelladresse
- ~~`LanStrategy::start + 0x11c` (`0xcd7840`): UNKLAR~~ — bestätigt (2026-11-08), siehe oben
- Gruppe-A-Referenzen: neue Adressen folgen erst aus der finalen Payload-Basis
- Die erforderliche ELF-/Segment-Umstrukturierung ist noch nicht durchgeführt.
- 3 UNKLAR-Stellen und 6 DIFF-Blöcke: separat, Maintainer-Disassemblierung
- Emulations- und Hardware-Tests: weiterhin ausstehend

**Fazit:** Die Payload-Platzierung ist derzeit blockiert. Es handelt sich nicht mehr um einen einfachen Byte-Patch, sondern um eine strukturelle ELF-Anpassung, die idealerweise über die Build-Toolchain erfolgen sollte.

### `ai_camera` — coexistence (`camera44-manifest.json`)

`znp::AICamera<UvcCamera>::LocalRtcMonitor(...)` present at identical size,
shift **+0x90b0**.

**Mapped (2026-11-08, Ghidra listing + symbol table).** Recorded per
`PORTING_METHODOLOGY.md`; not yet applied, emulation- or hardware-tested.
The patch redirects a call to another function, so its target was re-derived
from the callee's new address: the callees shifted by a different amount than
`LocalRtcMonitor`, and the `after` bytes change.

| Field | 02.01.01.07 (manifest) | 02.01.01.13 (`e8df8e57…`) |
|---|---|---|
| Function | `LocalRtcMonitor` @ `0x1296B4` | @ `0x132764` (`+0x90B0`) |
| Patch point VA (A) | `0x1297CC` | `0x13287C` |
| File offset | `0x1197CC` | `0x12287C` (VA − `0x10000`) |
| `znp::VideoStreamer::pause()` | `0x163300` | `0x16A748` (`+0x7448`) |
| `znp::VideoStreamer::resume()` (T) | `0x1633A0` | `0x16A7F8` (`+0x7458`; `pause()` grew by `0x10`) |
| before | `cb e6 00 eb` = `bl pause` | `b1 df 00 eb` = `bl pause` |
| after | `f3 e6 00 eb` = `bl resume` | `dd df 00 eb` = `bl resume` |

Arithmetic: `delta = 0x16A7F8 − 0x13287C − 8 = 0x37F74` (divisible by 4, in
range), `imm24 = 0xDFDD`, word `0xEB00DFDD`.

Verified: names and function heads in both listings (`_ZN3znp13VideoStreamer5pauseEv`,
`_ZN3znp13VideoStreamer6resumeEv`, unique); bytes at file offset `0x12287C` of
the 02.01.01.13 `ai_camera` read `b1 df 00 eb` (target `0x16A748`); the second
`resume()` call in `LocalRtcMonitor` (`0x132990`, `98 df 00 eb`) also targets
`0x16A7F8`; bytes at `0x1197CC` of the V4.4.3 `ai_camera` read `f3 e6 00 eb`.

### `ec-eeb001-gui` — Z-offset (`patch_zoffset44.py`)

The GUI was also recompiled (stock `24d0e561…` → 02.01.01.13 `8d6cac2a…`). The
single Z-menu branch patch needs its own mapping: the stock GUI symbols must be
compared against the new GUI (4443 named functions) to locate the menu-entry
function and re-derive the one branch. This is **not** covered by a constant
shift and is the one site that needs manual disassembly.

**Mapped (2026-11-08, Ghidra Version Tracking + listing check).** Recorded per
`PORTING_METHODOLOGY.md`; not yet applied, emulation- or hardware-tested.

| Field | V4.4.3 GUI (patched 02.01.01.07) | 02.01.01.13 GUI (`8d6cac2a…`) |
|---|---|---|
| Function | `set_event_cb` @ `0x75268`, 1104 bytes | `set_event_cb` @ `0x74760`, 1132 bytes |
| Per-function offset | | `-0xB08` (Exact Symbol Name Match; function grew `+0x1C`, not between A and T) |
| Caller | `setting_set_home_create` | (same name, unique match) |
| Site | switch @ `0x75290`, `caseD_5` | switch @ `0x74788`, `caseD_5` |
| Patch point VA (A) | `0x752D0` | `0x747C8` |
| File offset | `0x652D0` | `0x647C8` (`.text`: VA `0x13178` = file `0x3178` in both) |
| Branch target VA (T) | `0x756D8` | `0x74BD0` (`add sp,sp,#0x18`; epilogue, then tail call `b setting_set_z_offset_create`) |
| before | `a8 e4 00 eb` = `bl data_get_print_status` @ `0xAE578` | `9c e7 00 eb` = `bl data_get_print_status` @ `0xAE640` |
| after | `00 01 00 ea` = `b` T | `00 01 00 ea` = `b` T |

Arithmetic: `delta = 0x74BD0 − 0x747C8 − 8 = 0x400` (divisible by 4, in range),
`imm24 = 0x100`, word `0xEA000100`. T − A is `0x408` in both builds, so the
branch bytes are unchanged; only the location moves.

Verified: instruction and context at A and T in both listings; bytes at file
offset `0x647C8` of the 02.01.01.13 GUI read `9c e7 00 eb`, and at `0x652D0` of
the V4.4.3 GUI read `00 01 00 ea`. In stock 02.01.01.13 a conditional branch at
`0x747D4` also reaches T; the patch makes the jump unconditional, skipping the
print-status check before opening the Z-offset menu.

### Dual Trust, Reactor, bootloader

`opt/inst/daemon-000/daemon-000` and `opt/lib/libcolib.so` are **byte-identical**
to 02.01.01.07, and `uboot`/`boot0` are identical too (see
`builder/current/base_profiles/02.01.01.13.json`). The Dual Trust patch and the
Reactor bindings carry over unchanged; the bootloader removes the cross-base
brick risk.

## What a maintainer still has to do (not done here)

1. (`ai_camera` mapped above; still to be applied and tested.)
   For each patched function in `elegoo_printer`: disassemble the
   relocated function and confirm the existing `before` window still matches at
   the shifted address, then re-target the branch destinations in each patch's
   `after`/`branches` to the new addresses. The uniform shift is the expected
   delta but every branch target must be checked, not assumed.
2. ~~Re-derive the one GUI Z-offset branch by disassembly.~~ Done (mapped
   above); still to be applied, emulation-tested and hardware-tested.
3. ~~Re-check the `device_status_offset` data member against the new `.data`.~~
   Statically validated: `0xbbc` unchanged (see the `device_status_offset`
   subsection above); hardware test still required.
4. Rebuild the ARM emulation tests (`experimental/firmware-port/test_*44_emulation.py`)
   against the new binaries.
5. Hardware-test on a printer before any release, per `docs/TESTING.md`.

Until a ported, emulation-tested and hardware-tested patch set exists, the
builder keeps `ported_patch_set=false` for this base and refuses to build.
