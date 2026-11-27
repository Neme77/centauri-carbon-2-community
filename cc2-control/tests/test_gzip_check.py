#!/usr/bin/env python3
"""cc2_gzip_unpacks_to() (src/gzip_check.h) gives the same answer as Python's zlib for good and damaged gzip copies.

Good copies: every compression level and strategy, small windows, stored blocks, the optional header fields
(name, comment, extra field, header CRC), empty and window-sized inputs and the real start page and locale files.
Damaged copies: every byte of a small copy changed three ways, many bytes of the real start page and a locale file
changed in the middle of the payload with header and trailer untouched, cut-off and lengthened copies, two members,
a wrong CRC-32 or length asked for. A copy counts as good only when zlib unpacks it as one gzip member, with no bytes
after it, to exactly the plain file, and the CRC-32 and length asked for are those of that file.
"""
import os
import random
import shlex
import struct
import subprocess
import sys
import tempfile
import zlib
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


def member(plain, level=9, strategy=zlib.Z_DEFAULT_STRATEGY, wbits=15, flags=0, name=b"", comment=b"", extra=b""):
    header = bytearray(b"\x1f\x8b\x08" + bytes([flags]) + b"\0\0\0\0\x02\x03")
    if flags & 4:
        header += struct.pack("<H", len(extra)) + extra
    if flags & 8:
        header += name + b"\0"
    if flags & 16:
        header += comment + b"\0"
    if flags & 2:
        header += struct.pack("<H", zlib.crc32(bytes(header)) & 0xFFFF)
    packer = zlib.compressobj(level, zlib.DEFLATED, -wbits, 9, strategy)
    body = packer.compress(plain) + packer.flush()
    return bytes(header) + body + struct.pack("<II", zlib.crc32(plain), len(plain) & 0xFFFFFFFF)


def zlib_says(copy, plain, crc, size):
    if crc != zlib.crc32(plain) or size != len(plain):
        return 0
    unpacker = zlib.decompressobj(31)
    try:
        out = unpacker.decompress(copy) + unpacker.flush()
    except zlib.error:
        return 0
    return int(unpacker.eof and unpacker.unused_data == b"" and out == plain)


cases = []


def case(copy, plain, crc=None, size=None, expect=None):
    crc = zlib.crc32(plain) if crc is None else crc
    size = len(plain) if size is None else size
    said = zlib_says(copy, plain, crc, size)
    if expect is not None:
        assert said == expect, f"zlib says {said}, the test expected {expect}"
    cases.append(struct.pack("<IIIB", len(copy), crc, size, said) + copy)


rng = random.Random(449)
text = b"".join(b"<div class=\"row\">%d cc2 control %s</div>\n" % (i, b"x" * (i % 13)) for i in range(400))
noise = bytes(rng.getrandbits(8) for _ in range(70000))
web = ROOT / "web"
page = (web / "index.html").read_bytes() if (web / "index.html").exists() else text * 40
locales = sorted((web / "locales").glob("*.json"))
locale = locales[0].read_bytes() if locales else text * 8
inputs = [b"", b"a", b"ab" * 3, text, noise, b"\0" * 32768, b"\0" * 32769, (text * 30)[:65537],
          noise[:32768] + noise[:32768], page, locale]

# Good copies, as zlib and gzip write them.
for plain in inputs:
    for level in (0, 1, 6, 9):
        case(member(plain, level), plain, expect=1)
for plain in (text, noise[:5000], (text * 30)[:65537]):
    for strategy in (zlib.Z_FILTERED, zlib.Z_HUFFMAN_ONLY, zlib.Z_RLE, zlib.Z_FIXED):
        case(member(plain, 9, strategy), plain, expect=1)
    for wbits in (9, 12):
        case(member(plain, 9, wbits=wbits), plain, expect=1)
    case(member(plain, flags=4 | 8 | 16 | 2, name=b"index.html", comment=b"c", extra=b"AB\x02\0xy"), plain, expect=1)
    case(member(plain, flags=8, name=b"de.json"), plain, expect=1)
for plain in (page, locale):
    found = subprocess.run(["gzip", "-c", "-9", "-n"], input=plain, capture_output=True)
    if found.returncode == 0:  # the gzip of the host, as the installer calls the printer's
        case(found.stdout, plain, expect=1)

# Every byte of a small copy changed three ways (a header text byte or a padding bit may still unpack to the file).
small = member(text[:3000], flags=8, name=b"f")
for at in range(len(small)):
    for change in (0x01, 0x80, 0xFF):
        bad = bytearray(small)
        bad[at] ^= change
        case(bytes(bad), text[:3000])

# The real start page and a locale file, damaged in the middle of the payload: header and trailer untouched.
for plain in (page, locale):
    good = member(plain)
    for _ in range(40):
        at = rng.randrange(20, len(good) - 20)
        length = rng.choice((1, 2, 16, 300))
        bad = bytearray(good)
        for i in range(at, min(at + length, len(good) - 8)):
            bad[i] ^= rng.randrange(1, 256)
        assert bad[:10] == good[:10] and bad[-8:] == good[-8:]
        case(bytes(bad), plain, expect=0)
    middle = len(good) // 2
    case(good[:middle] + bytes(b ^ 0xFF for b in good[middle:middle + 16]) + good[middle + 16:], plain, expect=0)

# Cut off, lengthened, two members, wrong file asked for.
good = member(text)
for cut in (1, 7, 8, 9, 100, len(good) - 18, len(good) - 10):
    case(good[:-cut], text, expect=0)
case(good + b"\0", text, expect=0)
case(good + good, text, expect=0)
case(good[:-8] + b"\0" + good[-8:], text, expect=0)
case(good, text, crc=zlib.crc32(text) ^ 1, expect=0)
case(good, text, size=len(text) + 1, expect=0)
case(good, text, size=len(text) - 1, expect=0)
case(member(text[:-1]), text, expect=0)
for byte, value in ((2, 7), (3, 0x20), (3, 0x80)):
    bad = bytearray(good)
    bad[byte] = value
    case(bytes(bad), text, expect=0)

with tempfile.TemporaryDirectory(prefix="cc2-gzip-check-") as directory:
    data = Path(directory) / "cases"
    data.write_bytes(b"".join(cases))
    binary = Path(directory) / "test_gzip_check"
    subprocess.run([os.environ.get("CC", "cc"), "-O2", "-std=c11", "-Wall", "-Wextra", "-Wpedantic", "-Werror",
                    "-D_POSIX_C_SOURCE=200809L", *shlex.split(os.environ.get("CC2_TEST_CFLAGS", "")),
                    str(ROOT / "tests/test_gzip_check.c"), "-o", str(binary)], check=True, timeout=120)
    runner = shlex.split(os.environ.get("CC2_TEST_RUNNER", ""))
    result = subprocess.run([*runner, str(binary), str(data)], capture_output=True, text=True, timeout=300)
    sys.stdout.write(result.stdout)
    sys.stderr.write(result.stderr)
    assert result.returncode == 0, f"test_gzip_check exited with {result.returncode}"

print("PASS: test_gzip_check.py")
