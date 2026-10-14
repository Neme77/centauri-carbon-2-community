# Compatibility with stock 02.01.00.00

Verified locally on 10 October 2026. No printer connection, installation or hardware test was performed.

**The reverse-distance correction works for the stock 02.01.00.00 library after adding its new exact-hash manifest entry. The original ZIP does not support that library as shipped.**

## Evidence

- Downloaded the [archived stock 02.01.00.00 package](https://github.com/suchmememanyskill/cc2-firmwares/raw/refs/heads/main/cc2-02.01.00.00-0ba7e15363653ade503eff80d9d3493a-release-abroad.zip.sig).
- Its SHA-256 is `219fc28e9845f5d70e3a4499b60404d80368e5aa7e52eafb3e5e9c17c5e02926`, identical to the stock package pinned by the [community builder](https://github.com/Neme77/centauri-carbon-2-community/blob/d630ac4a37ec9a7d1644deef87e796e903b5b29a/builder/current/core/firmware_builder.py).
- Verified all three ELEG payload hashes and stock RSA signatures, the decrypted OTA version and manifest/SWU hash linkage, and the builder's pinned inner SWU, decrypted SWU and RootFS hashes.
- Extracted only `/opt/lib/libelegoo_extras.so` from that RootFS for patch inspection.
- The original library SHA-256 is `33b40a530c8c1ffe27e63a7d9f0fd68f3ccfae2cec44edfce10566831b085c41` (25,587,544 bytes). It differs from all four libraries in the original package. The original patcher correctly refuses it.
- Located the exported `Canvas::CMD_canvas_motor_control` handler by ELF symbol. Its start is `0xCB92E8`, size 2060 bytes. The faulty comparison block is at file offset and virtual address **`0xCB97E0`** (13342688). This is a new offset; do not use offsets for earlier releases.
- Disassembled the actual signed-distance load at `fp-0x124`, its conversion, the movement magnitude, stopping comparison and branch. The comparison still contains the negative-distance bug. The same 24-byte correction is valid at the new offset.
- Reproduced the original immediate-stop result for a negative request and zero movement. The corrected block passed **800 ARM emulation cases** covering positive/negative distances and movement, below/equal/above-threshold values and deterministic additional distances in the supported range. Forward comparisons and branch destinations were preserved.
- Applied the patch only to a separate local copy. File length and all bytes outside the 24-byte block were unchanged. Output SHA-256: `99850496248afce83e19d8feae0ee45375195fee6fbe24d0afdfec88c66eea83`.
- Verified check-only behavior, version mismatch refusal, unknown/altered/truncated input refusal, protection against overwriting the original or existing output, and detection/refusal of a second application.

The package contains tools, manifest, documentation and verification evidence. It contains no vendor library or firmware image. The supplied original ZIP has been preserved unchanged.

## Use on a local backup

Check the actual library backed up from the printer; the firmware version label alone is insufficient:

```sh
python3 patch_library.py --input /path/to/original/libelegoo_extras.so --version 02.01.00.00
```

Create a separate corrected local copy only if the check succeeds:

```sh
python3 patch_library.py --input /path/to/original/libelegoo_extras.so --version 02.01.00.00 --apply --output /path/to/libelegoo_extras.fixed.so
```

Optional reproduction with Capstone and Unicorn installed:

```sh
python3 verify_extended.py --library /path/to/original/libelegoo_extras.so
```

## Scope

This confirms the manual reverse-distance comparison correction for the exact archived 02.01.00.00 library. A community installation may have a changed library; the exact hash check remains mandatory.

It does not validate physical motor movement/stopping, controller communication, sensor-based full ejection, the native touchscreen, web buttons or deployment. It does not change TIMEOUT=0 behavior. This archive is not a printer Offline Update package. Hardware testing and a verified installation/restore route remain necessary before describing the fix as printer-tested.
