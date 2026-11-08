# Emulations-Ergebnisse: Druckerport 02.01.01.13 (lokal)

Arbeitsdokument, nur lokal. Ergebnisse der Unicorn-Emulation gegen die gepatchten,
verifizierten Binaries. Vendor-Funktionen werden abgefangen; **HTTP/Netzwerk-,
Koexistenz- und Hardware-Integration sind damit NICHT bewiesen** — Hardware-Test
bleibt erforderlich, bevor `ported_patch_set=true`.

## Gepatchte, verifizierte Binaries

| Binary | SHA-256 | statische Prüfung |
|---|---|---|
| `elegoo_printer.k1` | `adb8529358f12c7f5f10dcfdd8a9e30a0c385cb6bfd8b404d8bcb42b5d6abcc6` | `verify_patched.py` 42 PASS |
| `ec-eeb001-gui.patched` | `668ab8cb490c74eb5bbd769b421f6e4ade4e91fba322e22257496b9645f9ebfe` | 1 Wort @0x647c8, before/after geprüft |
| `ai_camera.patched` | `90da7634c21b74b3bf943d68129da78668b47f9706620e158283697ab95553a7` | 1 Wort @0x12287c, before/after geprüft |

## Emulations-Matrix (Phase α)

| E-Fall | Harness | Ergebnis | Kriterium |
|---|---|---|---|
| E3/E4/E5 HTTP-Guards + `device_status_offset 0xbbc` | `emulate_e345.py` | **128 PASS, 0 FAIL** | 32 Status × 4 Pfade korrekt; `0xbbc` statt Stale `0xbb4`; 409 im Nicht-idle-Fall |
| E6 LanStrategy::stop ohne HTTP-Stop | `emulate_e6.py` | **PASS** | `stop_http_server_file` nicht aufgerufen; Funktion läuft; MQTT-Stop (`0x8272c8`) aktiv |
| E1 GUI Z-Offset | `emulate_e1.py` | **PASS** | Pfad `0x747c8→0x74bd0→setting_set_z_offset_create (0x811f8)`; `data_get_print_status` übersprungen |
| E2 Kamera pause→resume | `emulate_e2.py` | **PASS** | erster Aufruf `resume 0x16a7f8`, nicht `pause 0x16a748` |
| E11 Dual Trust / Reactor | — | byte-identisch zu 02.01.01.07 (keine neue Emulation nötig) | `daemon-000`/`libcolib.so` unverändert |

## Grenzen / offen

- Emulation deckt nur den **Kontrollfluss** mit abgefangenen Vendor-Funktionen ab.
- **Nicht** geprüft: echtes HTTP/Netzwerk/MQTT/Kamera-Zusammenspiel, UI, Boot.
- Nächster Schritt: **Hardware-Test** (`HARDWARE_TEST_*.md`, `ROLLBACK_PLAN.md`),
  mit Backup und geprüftem, reproduzierbarem Rollback-Weg.
- `ported_patch_set=true` erst nach bestandenem Hardware-Test.
