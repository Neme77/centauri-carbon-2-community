# Relocation analysis: ELEGOO base 02.01.01.13

This directory holds the relocation analysis for porting the existing
`experimental/firmware-port/` community patches from ELEGOO base
**02.01.01.07** to **02.01.01.13**. It is analysis only. No ported byte
patches, manifests or vendor binaries are committed here yet; those are written
and hardware-tested by a maintainer, following `AGENTS.md` and
`UPDATE_PLAN_02.01.01.13.md`.

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

### `ai_camera` — coexistence (`camera44-manifest.json`)

`znp::AICamera<UvcCamera>::LocalRtcMonitor(...)` present at identical size,
shift **+0x90b0**.

### `ec-eeb001-gui` — Z-offset (`patch_zoffset44.py`)

The GUI was also recompiled (stock `24d0e561…` → 02.01.01.13 `8d6cac2a…`). The
single Z-menu branch patch needs its own mapping: the stock GUI symbols must be
compared against the new GUI (4443 named functions) to locate the menu-entry
function and re-derive the one branch. This is **not** covered by a constant
shift and is the one site that needs manual disassembly.

### Dual Trust, Reactor, bootloader

`opt/inst/daemon-000/daemon-000` and `opt/lib/libcolib.so` are **byte-identical**
to 02.01.01.07, and `uboot`/`boot0` are identical too (see
`builder/current/base_profiles/02.01.01.13.json`). The Dual Trust patch and the
Reactor bindings carry over unchanged; the bootloader removes the cross-base
brick risk.

## What a maintainer still has to do (not done here)

1. For each patched function in `elegoo_printer`/`ai_camera`: disassemble the
   relocated function and confirm the existing `before` window still matches at
   the shifted address, then re-target the branch destinations in each patch's
   `after`/`branches` to the new addresses. The uniform shift is the expected
   delta but every branch target must be checked, not assumed.
2. Re-derive the one GUI Z-offset branch by disassembly.
3. Re-check the `device_status_offset` data member against the new `.data`.
4. Rebuild the ARM emulation tests (`experimental/firmware-port/test_*44_emulation.py`)
   against the new binaries.
5. Hardware-test on a printer before any release, per `docs/TESTING.md`.

Until a ported, emulation-tested and hardware-tested patch set exists, the
builder keeps `ported_patch_set=false` for this base and refuses to build.
