# Qualified 02.01 adapter ABI

Offsets were checked against locally extracted stock 02.01.00.00 ELF symbols
and ARM disassembly. Vendor files are not distributed here. The older public
[ELEGOO source](https://github.com/elegooofficial/CentauriCarbon2) at
`5a2ea7fc03e707552701b1a69f463699cbd39230` provides protocol context, but is not an ABI substitute: the stock
stop signature has an additional boolean and the object layouts differ.

| Input | SHA-256 |
|---|---|
| Stock `/opt/lib/libelegoo_extras.so` | `33b40a530c8c1ffe27e63a7d9f0fd68f3ccfae2cec44edfce10566831b085c41` |
| Stock `/opt/bin/ec-eeb001-gui` | `a231c26bc965b0e2ee5edbf4fc1ca4018fed009da601a7e7dc267a0c81e7fb08` |
| Community Z-offset-patched GUI | `afa2b1f181d18dc4803a60ee61fb3fc454f1e91afa12743bc1a1e5e386e439ff` |

The existing builder's GUI patch writes `000100ea` at file offset `0x63c5c`.
The screen component requires that resulting GUI hash. Its executable is
ARM32 `ET_EXEC`; GUI constants in `qualified.h` are absolute addresses.
Canvas symbols are resolved with `dlsym`, so library load addresses may vary.

| Canvas symbol/function | Stock symbol address |
|---|---|
| `Canvas::CMD_canvas_motor_control(shared_ptr<GCodeCommand>)` | `0xcb92e8` |
| `CanvasProtocol::PARSE_CANVAS_ALL_STATUS(vector<uint8_t> const&)` | `0xc8d8a0` |
| `Canvas::auto_prefeed_filament()` | `0xca65d4` |
| `Canvas::handle_ready()` | `0xc96f18` |
| `Canvas::handle_shutdown()` | `0xc9d338` |
| `CanvasProtocol::feeder_filament_control(uint8_t, FeederMotor const&, bool)` | `0xc8adb0` |
| `CanvasProtocol::feeders_filament_stop(uint8_t, bool, bool)` | `0xc8aaa4` |
| `CanvasProtocol::rocker_control(uint8_t, int8_t, bool)` | `0xc8b33c` |

The motor callback receives the nontrivial by-value `shared_ptr` through an
ABI-indirect pointer in r1. Its first word is the `GCodeCommand*`. `get_int`
uses the new libstdc++ string ABI. `FeederMotor` consists of three signed
16-bit values: distance, speed and acceleration.

The `Canvas` protocol pointer is at `+0x0`, reactor at `+0x20`, and print-state
`std::string` at `+0x2f4`. `CanvasProtocol` status begins at `+0xe8`:

| Status-relative member | Offset |
|---|---|
| Four double channel positions | `0x08 + channel * 8` |
| Per-channel fault flags | `0x48`, `0x50`, `0x54`, `0x58`, `0x60` plus channel |
| Per-channel moving flags | `0x4c + channel` |
| Four inlet sensor flags | `0x5c + channel` |
| MCU connection flag | `0x64` |
| Signed current feed channel | `0x610` |
| Toolhead filament sensor | `0x61a` |

The status hook timestamps actual all-status frames, serialized with sample
reads. It rejects vectors shorter than the parser's required payload. These
are inlet signals; absence is not a measurement of external tube length.
The stock boot-initialized flag is at protocol `+0x94c`; the first initialization
frame does not qualify telemetry. A subsequent regular frame is required.

For the GUI, `create_filament_multi_view` is `0x407e0`; `filament_view` is
`0x24e4b8`, with selection at `+0x44`. The existing selection conversion maps
UI 1â€“4 directly to channels 0â€“3 and 0 to no selection. The existing machine
status function reports 1 for idle. Other function constants and the eight
original entry bytes are in `qualified.h`.

LVGL coordinates are signed 16-bit. Click event 7, delete event 33 and center
alignment 9 agree with the public LVGL 8.3 headers
([events](https://github.com/lvgl/lvgl/blob/v8.3.11/src/core/lv_event.h),
[coordinates](https://github.com/lvgl/lvgl/blob/v8.3.11/src/misc/lv_area.h)).

Entry trampolines preserve two ARM instructions, then jump back to entry+8.
Each qualified pair contains no PC-relative instruction or branch; the
prefeed entry includes a VFP push. Hook entry fingerprints are checked before
writing, and instruction caches are flushed after mutation. Unknown entries
are rejected rather than copied speculatively. Requalify all hashes, offsets,
signatures, entries and live hardware behavior for another firmware version.
