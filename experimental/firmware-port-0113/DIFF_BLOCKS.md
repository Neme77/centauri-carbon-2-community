# DIFF-Blöcke: Zuordnung und Eingrenzung (lesend)

Arbeitsdokument, nur lokal. Ordnet die 6 `before: DIFF`-Stellen ihren Funktionen
und Umbau-Kontexten zu. **Keine Patch-Bytes, keine Adresse final, keine
Spekulation über Implementierung oder Verhalten.**

## Kernbefund

Alle 6 DIFF-Funktionen sind im neuen Build **gleich groß** wie im alten,
**`+0x7790`** verschoben, und ihre before-Fenster weichen **ausschließlich durch
relozierte Operanden** ab (`bl`/`b`-Ziele, `movw`/`movt`-Globale, `ldr [pc]`),
mit **null echten Opcode-Unterschieden**. Es handelt sich also nicht um echte
Code-Umbauten, sondern um dieselbe Lage wie bei den 3 eingegrenzten UNKLAR-Stellen
— reine Relozierung. Das reduziert die Arbeit vom Reverse-Engineering auf das
Neu-Herleiten der relozierten Operanden (plus, bei den Payload-Fällen, die
Payload-Basis).

| Stelle | before-Worte | abweichend | relozierte Operanden | echte Opcode-Unterschiede |
|---|---:|---:|---:|---:|
| `0x812128` | 28 | 10 | 10 | **0** |
| `0x84f940` | 111 | 31 | 31 | **0** |
| `0x837168` | 1 | 1 | 1 | **0** |
| `0x8371a0` | 1 | 1 | 1 | **0** |
| `0x851754` | 1 | 1 | 1 | **0** |
| `0x851924` | 1 | 1 | 1 | **0** |

Einschränkung: Der Struktur-Vergleich betrifft das **before**-Fenster. Die
**after**-Seite (Ersatz-Code) muss weiterhin pro Operand neu kodiert werden; die
genaue Bestätigung liefert die Emulation (`TEST_MATRIX.md`, E10).

## Gruppe 1 — Block-Einbauten (ausgelagerter Ersatz-Code)

### `0x812128` — `NetworkManager::set_mode(NetworkMode)`

| Feld | Wert |
|---|---|
| Funktion alt → neu | `0x811e0c` → `0x81959c` |
| Größe alt / neu | `0x694` / `0x694` (gleich) |
| Shift | `+0x7790` |
| Referenzform | Block-Einbau, after 112 B mit eigenen Branches |
| before-Fenster | 28 Worte, nur Relozierung |
| Externe Abhängigkeiten (after) | `LanStrategy::start_http_server_file` `0x81f93c` → voraussichtlich `0x8266a0` (`+0x7790`, Einsprung belegt) |
| Interne Branches (after) | in-Funktion (`0x812198`, `0x812498`) — bei `+0x7790` lagegleich |
| Offene Fragen | after-interne Branch-Bytes neu kodieren; E10-Emulation; keine Payload-Abhängigkeit |

### `0x84f940` — `HTTPServer::handle_file_upload(...)`

| Feld | Wert |
|---|---|
| Funktion alt → neu | `0x84ebb8` → `0x856348` |
| Größe alt / neu | `0x21ec` / `0x21ec` (gleich) |
| Shift | `+0x7790` |
| Referenzform | Block-Einbau, after 444 B mit eigenen Branches |
| before-Fenster | 111 Worte, nur Relozierung |
| Externe Abhängigkeiten (after) | `handle_file_cancel` `0x8512e0`→`0x858534`-Bereich · `remove_client` `0x851fc8`→`0x859758` · `DeviceStatus::getInstance` `0x87ab00`→`0x882290` · `set_status` `0x88e688`→`0x895e18` · `set_status` `0x88eacc`→`0x89625c` · `set_sub_status` `0x88ee04`→`0x896594` (alle `+0x7790`, Einsprünge belegt) |
| Interne Branches (after) | mehrere in-Funktion — bei `+0x7790` lagegleich |
| Offene Fragen | alle externen Ziele einzeln bestätigen (Einsprünge Off 0, zuverlässig); after-Bytes neu kodieren; E5/E10; keine Payload-Abhängigkeit |

## Gruppe 2 — Einzel-`bl`-Ersatz (Ziel im Payload)

Diese vier sind keine Block-Umbauten, sondern einzelne `bl`-Ersetzungen. Das
before ist ein `bl` auf eine Vendor-Funktion (deren Verschiebung das DIFF
auslöst), das after zielt in den **Payload-Block** — sie erben damit die
Payload-Platzierungs-Abhängigkeit (`README.md`, Abschnitt „Payload placement“).

| Stelle | Funktion alt → neu | Größe | Shift | Referenzform | after-Ziel (Payload) | Abhängigkeit |
|---|---|---|---|---|---|---|
| `0x837168` | `AsyncMQTTConnector::AsyncMQTTConnector` `0x836f78`→`0x83e708` | `0x558` gleich | `+0x7790` | einzelner `bl` | `payload+0x1c4` | Payload-Basis |
| `0x8371a0` | `AsyncMQTTConnector::AsyncMQTTConnector` `0x836f78`→`0x83e708` | `0x558` gleich | `+0x7790` | einzelner `bl` | `payload+0x1dc` | Payload-Basis |
| `0x851754` | `HTTPServer::check_token(HttpRequest*)` `0x8515d8`→`0x858d68` | `0x264` gleich | `+0x7790` | einzelner `bl` | `payload+0x124` | Payload-Basis |
| `0x851924` | `HTTPServer::check_token(string&)` `0x85183c`→`0x858fcc` | `0x1b4` gleich | `+0x7790` | einzelner `bl` | `payload+0x124` | Payload-Basis |

Offene Fragen Gruppe 2:
- before: der verschobene Vendor-`bl` ist an der `+0x7790`-Stelle strukturell
  belegt (ein `bl`); die genaue neue before-Bytefolge ergibt sich aus dem neuen
  Build.
- after: Ziel = Payload-Basis + Offset — erst nach Payload-Platzierung rechenbar.

## Zusammenfassung

- **6/6 DIFF-Blöcke sind relozierungsbedingt, kein echter Umbau.** Gleiche Größe,
  `+0x7790`, before-Fenster nur relozierte Operanden.
- **Gruppe 1 (2 Block-Einbauten)** hängt nicht am Payload; die externen Ziele
  sind Funktionsanfänge (`+0x7790`, belegt). Offen: after-Operanden neu kodieren,
  Emulation.
- **Gruppe 2 (4 Einzel-`bl`)** hängt an der Payload-Platzierung.
- Keiner braucht ein Reverse-Engineering der Funktionslogik; alle brauchen
  Operanden-Neukodierung und Emulations-/Hardware-Test.

## Before-Fenster im neuen Build (lesend verifiziert)

Lesendes Auslesen der before-Fenster an `alt + 0x7790`. Baseline „alt“ ist für
`0x812128` der Stock-Build (`http44`-Manifest), für die `network44`-Stellen die
bereits HTTP-gepatchte Ausgangsbinary (`256bb3b9…`, = Manifest-Input). Keine
After-Bytes, keine Operanden-Kodierung.

| Alte Adresse | Neue Adresse | Erw. (+0x7790) | Shift? | Before-Bytes neu | Before-Bytes alt (falls vergleichbar) | Vergleich | Kontext | Status |
|---|---|---|---|---|---|---|---|---|
| `0x812128` | `0x8198b8` | `0x8198b8` | Ja | `afe0e2eb0040a0e1f8004be2543a04e3b83040e3e020a0e3e01d04e3b81040e356cbe2eb703f04e3b83040e318300be518301be5ec300be518001be51db2e2eb0030a0e1e8300be504208de2ec304be2030093e8030082e80230a0e300308de5f8304be20e0093e80400a0e15ec2e4eb` | `47f5e2eb0040a0e1f8004be27c3207e3b93040e3e020a0e3081607e3b91040e3eedfe2ebb43707e3b93040e318300be518301be5ec300be518001be5b5c6e2eb0030a0e1e8300be504208de2ec304be2030093e8030082e80230a0e300308de5f8304be20e0093e80400a0e1f6d6e4eb` | erwartbar abweichend (28 Worte, 10 reloz., 0 echt) | `http44`, Baseline Stock | Before dokumentiert |
| `0x812158` | `0x8198e8` | `0x8198e8` | Ja | `18301be5` | `ce0000ea` (HTTP-Patch-Artefakt, kein Stock-Code) | nicht vergleichbar | `network44`; alt = HTTP-gepatcht, neu = Stock; der network-Patch setzt den HTTP-Patch voraus | nicht vergleichbar |
| `0x837168` | `0x83e8f8` | `0x83e8f8` | Ja | `3902e2eb` | `5620e2eb` | erwartbar abweichend (1 Wort, `bl`-Ziel reloziert) | `network44` | Before dokumentiert |
| `0x8371a0` | `0x83e930` | `0x83e930` | Ja | `2b02e2eb` | `4820e2eb` | erwartbar abweichend (1 Wort, `bl`-Ziel reloziert) | `network44` | Before dokumentiert |
| `0x851754` | `0x858ee4` | `0x858ee4` | Ja | `339ce1eb` | `4dbae1eb` | erwartbar abweichend (1 Wort, `bl`-Ziel reloziert) | `network44` | Before dokumentiert |
| `0x851924` | `0x8590b4` | `0x8590b4` | Ja | `bf9be1eb` | `d9b9e1eb` | erwartbar abweichend (1 Wort, `bl`-Ziel reloziert) | `network44` | Before dokumentiert |

**Zusammenfassung Before-Seite:**
- Dokumentierte before-Fenster: **6/6**.
- Bestätigte `+0x7790`-Shifts: **6/6** (neue Adresse = alt + 0x7790).
- Nicht vergleichbar: **1** (`0x812158`). Grund: Die `network44`-before `ce0000ea`
  ist ein **HTTP-Patch-Artefakt** (vom vorgelagerten HTTP-Patch geschriebener
  Branch), der neue Build ist Stock (`ldr 18301be5`). Ein Stock-zu-Stock-Vergleich
  ist hier nicht möglich; der Patch setzt den HTTP-Patch voraus. Kein Hinweis auf
  echten Umbau — nur eine Patch-Abhängigkeit.
- Before-Seite aller 6 DIFF-Blöcke ist damit dokumentiert (5 vergleichbar als
  reine Relozierung, 1 mit ausdrücklich dokumentiertem Nicht-Vergleich).
- Einordnung „reine Relozierung, kein echter Umbau“ wird dadurch **gestärkt** (5/6
  before-Fenster zeigen null echte Opcode-Unterschiede); für `0x812158` weder
  bestätigt noch widerlegt, aber durch die Patch-Abhängigkeit erklärt.
- **Weiterhin offen (After-Arbeit):** Operanden-/Branch-Neukodierung aller DIFF-after,
  Payload-Abhängigkeit der Gruppe 2, Block-after-Branches der Gruppe 1,
  Emulation (E5/E10) und Hardware-Test.

## Offene Abhängigkeiten

- Payload-Basis (für Gruppe 2 und die Payload-referenzierenden Branches).
- Einzelbestätigung der externen Ziele von `0x84f940` (6 Funktionen, Einsprünge).
- Emulation E5/E10, danach Hardware.
- Dies bleibt Maintainer-Arbeit: Operanden-Neukodierung = Patch-Bytes, hier nicht geschrieben.
