# Patch sites: every byte this port writes

Every row below was read directly from the stock/patched binaries or the
http44/network44 manifests (not transcribed from memory). Addresses are the
**new build's** virtual address unless marked "old". File offset = VA −
`0x10000` throughout (both builds share this mapping for `.text`).

## `elegoo_printer`

### 1. ELF restructure (K1) — 16 structural fields, behavior-neutral

No code bytes. `LOAD[4]`/`DYNAMIC`/`PT_TLS` `p_offset` and 10 section-table
`sh_offset` entries shift by `+0x10000`; `LOAD[3]` (`.text`) `p_filesz`/
`p_memsz` extend to make room for the 500 B payload at file `0xcb4fac` (VA
`0xcc4fac`). Full before/after table: `WORK/corrected/report.json` (generated
by `apply_patches.py`) or `K1_RESTRUCTURE_SPEC.md`.

### 2. 17 payload-internal branches (inside the 500 B payload, file `0xcb4fac`+offset)

| Payload off. | Type | Before | After | Target VA | Purpose |
|---|---|---|---|---|---|
| `0x34` | `bl` | `00000000` | `ae85edeb` | `0x8266a0` | `LanStrategy::start()` |
| `0x38` | `b` | `00000000` | `0f53edea` | `0x819c28` | `set_mode` internal (+0x68c) |
| `0x40` | `bl` | `00000000` | `6a4dd6eb` | `0x25859c` | `NetworkManager::get_instance` (+0x2530 shift) |
| `0x58` | `b` | `00000000` | `ec85edea` | `0x8267bc` | `LanStrategy::start+0x11c` (confirmed by full-function disassembly, 161/161 instructions match — `OPEN_QUESTIONS.md` #1) |
| `0x94` | `bl` | `00000000` | `a088edeb` | `0x8272c8` | `stop_mqtt_client` |
| `0xdc` | `bl` | `00000000` | `5386edeb` | `0x8269dc` | `LanStrategy::report_message` |
| `0xfc` | `b` | `00000000` | `4b86edea` | `0x8269dc` | `LanStrategy::report_message` |
| `0x10c` | `b` | `00000000` | `2280edea` | `0x825148` | `WanStrategy::report_message` |
| `0x110` | `bl` | `00000000` | `364dd6eb` | `0x25859c` | `get_instance` (+0x2530) |
| `0x114` | `bl` | `00000000` | `da52edeb` | `0x819c30` | `NetworkManager::get_mode` |
| `0x120` | `b` | `00000000` | `aa41efea` | `0x89577c` | `DeviceStatus::post_change_message+0x57c` |
| `0x12c` | `bl` | `00000000` | `b6ebcfeb` | `0xbffb8` | `strcmp` (rebuilt PLT stub, −0xd8 shift) |
| `0x13c` | `bl` | `00000000` | `68f4eeeb` | `0x882290` | `DeviceStatus::getInstance` |
| `0x170` | `bl` | `00000000` | `7ae9cfeb` | `0xbf70c` | `std::string::append` (PLT, −0xe4) |
| `0x17c` | `bl` | `00000000` | `a2ebcfeb` | `0xbffb8` | `strcmp` (PLT, −0xd8) |
| `0x1d8` | `b` | `00000000` | `16e8cfea` | `0xbf1e4` | `std::string` ctor (PLT, −0xe4) |
| `0x1f0` | `b` | `00000000` | `10e8cfea` | `0xbf1e4` | `std::string` ctor (PLT, −0xe4) |

"Before" is `00000000` here because, in the body this port currently ships
(`WORK/elegoo_printer.k1`), those 4-byte slots were never populated with the
real 02.01.01.07 payload logic in the first place — **see
`OPEN_QUESTIONS.md` point 0.** In the corrected build
(`WORK/corrected/elegoo_printer.patched`, produced by `apply_patches.py`) the
"before" at each of these offsets is the real network44-manifest payload
body instruction at that offset, not zero; the 17 words above are identical
in both builds (only the surrounding ~353 bytes differ).

### 2b. 6 additional payload-internal branches + 2 string-pointer relocations

Found by `scan_payload_branches.py` after a real-hardware crash on
2026-11-08 traced to the `0x11c` entry below; these were missing from the
original "17 branches" list (section 2) and were never relocated in either
prior corrected build. See `OPEN_QUESTIONS.md` for the crash analysis.

| Payload off. | Type | Old target (stale) | New target (fixed) | Target symbol |
|---|---|---|---|---|
| `0x00c` | `b.c1` (cond 1) | `0x7ffc5c` (garbage) | `0x819c28` | `NetworkManager::set_mode+0x68c` |
| `0x018` | `b.c0` (cond 0) | `0x7ffc5c` (garbage) | `0x819c28` | same |
| `0x024` | `b.c1` | `0x7ffc5c` (garbage) | `0x819c28` | same |
| `0x030` | `b.c0` | `0x7ffc5c` (garbage) | `0x819c28` | same |
| `0x050` | `b.c0` | `0x80c950` (garbage) | `0x82691c` | `LanStrategy::start+0x27c` |
| `0x11c` | `b.c1` | `0x87b538` (garbage, **the 2026-11-08 crash site**) | `0x895504` | `post_change_message+0x304` |
| `0x1d0`/`0x1d4` (movw/movt r1, cond EQ) | data | `0xcd7974` (→ `spdlog::details::days` in new build) | `0xcc5138` | payload's own `"CC2_LAN_MQTT_CHECK_TICK"` string |
| `0x1e8`/`0x1ec` (movw/movt r1, cond EQ) | data | `0xcd798c` (→ `spdlog::details::days+0x18`) | `0xcc5150` | payload's own `"CC2_LAN_MQTT_RETRY_CONNECTION"` string |

### 3. 2 verbatim block-inserts

| Old VA | New VA | File offset | Size | Source |
|---|---|---|---|---|
| `0x812128` | `0x8198b8` | `0x8098b8` | 112 B | `http44-manifest.json`, `va="0x812128"`, `after` field, copied verbatim |
| `0x84f940` | `0x8570d0` | `0x8470d0` | 444 B | `http44-manifest.json`, `va="0x84f940"`, `after` field, copied verbatim |

Both are relocation-invariant (every internal branch and external call target
inside them shifts by the same `+0x7790` as the block itself, so the bytes
need no recoding — verified byte-for-byte against both manifests).

### 4. 10 verbatim data/NOP/in-function patches

| Old VA | New VA | File offset | Before | After | Purpose |
|---|---|---|---|---|---|
| `0x81f1b4` | `0x826944` | `0x816944` | `ff0100eb` | `00f020e3` | NOP — `LanStrategy::stop` no longer calls `stop_http_server_file` |
| `0x811c0c` | `0x81939c` | `0x80939c` | `0200a0e1` | `08101be5` | `NetworkManager` owner arg before coexistence teardown |
| `0x812120` | `0x8198b0` | `0x8098b0` | `0200a0e1` | `a8111be5` | same, `set_mode` |
| `0x84ec20` | `0x8563b0` | `0x8463b0` | `14301be50b0053e3` | `020053e30b005313` | admit `PRINTING=2` in upload status check |
| `0x850238` | `0x8579c8` | `0x8479c8` | `12f900eb` | `d4fdffeb` | write-error guard: don't force idle from active states |
| `0x8509a8` | `0x858138` | `0x848138` | `36f700eb` | `f8fbffeb` | MD5-error guard: same |
| `0x88db48` | `0x8952d8` | `0x8852d8` | `000053e30130a0030030a013` | `010053e30130a0930030a083` | restore v2 LAN formatter in LAN+WAN modes |
| `0x812494` | `0x819c24` | `0x809c24` | `00f020e3` | `24ffffea` | ensure HTTP started after strategy selection |
| `0x850000` | `0x857790` | `0x847790` | `b1fa00eb` | `5afeffeb` | upload status guard (in-function `bl`) |
| `0x8509c4` | `0x858154` | `0x848154` | `0ef900eb` | `f9fbffeb` | completion substatus guard (in-function `bl`) |

### 5. 12 payload-reference branches (point into the payload at `0xcc4fac`+offset)

| Old VA | New VA | File offset | Before | After | Payload offset |
|---|---|---|---|---|---|
| `0x811c10` | `0x8193a0` | `0x8093a0` | `33ff2fe1` | `18af12eb` | `+0x5c` |
| `0x811c70` | `0x819400` | `0x809400` | `33ff2fe1` | `12af12eb` | `+0xa4` |
| `0x812124` | `0x8198b4` | `0x8098b4` | `33ff2fe1` | `d3ad12eb` | `+0x5c` |
| `0x81f028` | `0x8267b8` | `0x8167b8` | `54304be2` | `0a7a12ea` | `+0x3c` |
| `0x88dd54` | `0x8954e4` | `0x8854e4` | `b10ffeeb` | `e9be10eb` | `+0xe4` |
| `0x88dd70` | `0x895500` | `0x885500` | `9d0000ea` | `edbe10ea` | `+0x110` |
| `0x88dfc4` | `0x895754` | `0x885754` | `150ffeeb` | `54be10eb` | `+0x100` |
| `0x812158` | `0x8198e8` | `0x8098e8` | `18301be5` | `afad12ea` | `+0x00` (inside block-insert #3 above) |
| `0x851754` | `0x858ee4` | `0x848ee4` | `339ce1eb` | `79b011eb` | `+0x124` |
| `0x851924` | `0x8590b4` | `0x8490b4` | `bf9be1eb` | `05b011eb` | `+0x124` |
| `0x837168` | `0x83e8f8` | `0x82e8f8` | `3902e2eb` | `1c1a12eb` | `+0x1c4` |
| `0x8371a0` | `0x83e930` | `0x82e930` | `2b02e2eb` | `141a12eb` | `+0x1dc` |

### 6. 2 cross-function branches (not payload; target `handle_file_upload`, `+0x7790` shift)

| Old VA | New VA | File offset | Before | After | Target |
|---|---|---|---|---|---|
| `0x850eb4` | `0x858644` | `0x848644` | `3c311be5` | `c5faffea` | `0x857160` (`handle_file_cancel` → `handle_file_upload` region) |
| `0x852500` | `0x859c90` | `0x849c90` | `3ff200eb` | `2af5ffeb` | `0x857140` (`handle_upload_timeout` → `handle_file_upload` region) |

## `ec-eeb001-gui`

| VA | File offset | Before | After | Purpose |
|---|---|---|---|---|
| `0x747c8` | `0x647c8` | `9ce700eb` | `000100ea` | `b 0x74bd0` — skip `data_get_print_status`, reach the Z-offset menu unconditionally |

## `ai_camera`

| VA | File offset | Before | After | Purpose |
|---|---|---|---|---|
| `0x13287c` | `0x12287c` | `b1df00eb` | `dddf00eb` | `bl 0x16a7f8` (`resume`, was `bl pause`) in `LocalRtcMonitor` |

## Totals

17 + 6 payload-internal branches + 2 string-pointer relocations + 12
payload-reference + 2 cross-function + 10 verbatim + 2 block-inserts (code) +
16 structural fields (no code) in `elegoo_printer`, plus 1 word each in
`ec-eeb001-gui` and `ai_camera`.
