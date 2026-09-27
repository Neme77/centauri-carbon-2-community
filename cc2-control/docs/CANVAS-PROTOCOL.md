# Canvas query evidence — 1.1.8

The previous mapping of method 2003 to channel information was incorrect.
Adjacent string and number tables must not be paired by index.

Verified directly from ARM instructions in the supplied `elegoo_printer`:

- `APIPrinter::create_api` at 0x6420c4 registers method 2005
  (`movw r3, #2005` at 0x642bd8).
- Its assignment call at 0x642c08 targets 0x658cf4, the symbol for
  `std::function::operator=` with `create_api` lambda #46.
- Lambda #46 at 0x641cdc calls 0x64e6f8 from 0x641d14.
  The target is `APIPrinter::handle_canvas_get_channel_info`.
- That handler calls `DeviceStatus::get_canvas_channel_info` at 0x61af0c,
  without reading request parameters. It sets the response result and error code.
- The data function writes the key `canvas_info` (string address 0x880b9c).
- Method 2003 is registered at 0x642b60, binds lambda #44 through
  0x658bf4, and calls `handle_canvas_edit_filament` at 0x64d9a0.

Read-only request through the existing registered MQTT API client:

```json
{"method":2005,"id":2005}
```

HTTP `accepted: true` means the MQTT request was sent, not that the firmware
accepted it. Hardware verification must inspect the response with method 2005
and its result/error_code. No live hardware success is claimed by this release.

The dashboard and fan-control layout is the restored 1.1.4 layout. Canvas
load/unload/material controls use the existing G-code paths, not method 2003.
