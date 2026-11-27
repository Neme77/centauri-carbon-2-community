#!/usr/bin/env python3
"""The display pack budget must be one contract on both sides.

cc2-control/src/display_theme.h enforces the request limit on the printer;
web-src/src/lib/display.ts must refuse the same sizes before uploading, so a
user never waits for an upload that the server is guaranteed to reject.
"""
import re
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
header = (ROOT / "src" / "display_theme.h").read_text(encoding="utf-8")
client = (ROOT / "web-src" / "src" / "lib" / "display.ts").read_text(encoding="utf-8")


def number(text, pattern):
    found = re.search(pattern, text)
    assert found, pattern
    return int(found.group(1))


MiB = 1024 * 1024
KiB = 1024
server = {
    "pack": number(header, r"#define DISP_MAX_PACK \((\d+) \* 1024 \* 1024\)") * MiB,
    "file": number(header, r"#define DISP_MAX_FILE \((\d+) \* 1024\)") * KiB,
    "assets": number(header, r"#define DISP_MAX_ASSETS (\d+)"),
}
assert server["pack"] == 4 * MiB, server
assert server["file"] == 512 * KiB, server
assert server["assets"] == 320, server

client_pack = number(client, r"export const MAX_PACK_BYTES = (\d+) \* 1024 \* 1024") * MiB
client_file = number(client, r"export const MAX_FILE_BYTES = (\d+) \* 1024") * KiB
client_assets = number(client, r"export const MAX_ASSETS = (\d+)")
assert client_pack == server["pack"], (client_pack, server["pack"])
assert client_file == server["file"], (client_file, server["file"])
assert client_assets == server["assets"], (client_assets, server["assets"])
assert re.search(r"export const BOOT_FRAMES = 100", client)

# The client must bound the boot frame by the remaining pack budget and check
# the finished tar before uploading; the server must still enforce its own cap.
assert "perFrame" in client and "bootFrame(" in client
assert "const size = tarBytes(files)" in client and "MAX_PACK_BYTES - PACK_MARGIN_BYTES" in client
assert "size > MAX_PACK_BYTES" in client, "buildTar must refuse an oversized pack"
assert re.search(r"length < 512 \|\| length > DISP_MAX_PACK", header)
assert re.search(r"size == 0 \|\| size > DISP_MAX_FILE", header)
print(f"PASS: display pack contract matches on both sides "
      f"({server['pack']} B pack, {server['file']} B file, {server['assets']} assets)")
