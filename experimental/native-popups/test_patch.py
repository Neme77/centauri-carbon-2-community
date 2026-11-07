# SPDX-License-Identifier: GPL-3.0-or-later
"""patch.py on an entirely synthetic ARM executable; the vendor program is never used.

The fixture's _start calls its "Home show" (returns 7) through a view-table word
and exits with the result. The payload calls the original and adds 35, so a
patched run exits with 42. Runs under qemu-arm when available.
"""
import hashlib
import os
import shutil
import struct
import subprocess
import tempfile
import unittest

import patch

HOME, VIEW, START = 0x10240, 0x10300, 0x10200
PAYLOAD_VA = 0x01400000


def elf_header(entry, phnum):
    ident = b'\x7fELF\x01\x01\x01' + bytes(9)
    return struct.pack('<16sHHIIIIIHHHHHH', ident, 2, 40, 1, entry, 52, 0, 0x5000400, 52, 32, phnum, 40, 0, 0)


def fixture(extra_note=False):
    raw = bytearray(0x1004)
    headers = [(1, 0, 0x10000, 0x10000, 0x1000, 0x1000, 5, 0x10000),
               (1, 0x1000, 0x21000, 0x21000, 4, 8, 6, 0x10000),
               (4, 0x400, 0x10400, 0x10400, 0x20, 0x20, 4, 4)]
    if extra_note:
        headers.append((4, 0x420, 0x10420, 0x10420, 0x20, 0x20, 4, 4))
    raw[0:52] = elf_header(START, len(headers))
    for i, h in enumerate(headers):
        struct.pack_into('<8I', raw, 52 + i * 32, *h)
    # _start: r3 = *view; blx r3; exit(r0)
    struct.pack_into('<5I', raw, START - 0x10000, 0xe59f3010, 0xe5933000, 0xe12fff33, 0xe3a07001, 0xef000000)
    struct.pack_into('<I', raw, START - 0x10000 + 0x18, VIEW)
    struct.pack_into('<2I', raw, HOME - 0x10000, 0xe3a00007, 0xe12fff1e)   # mov r0, #7; bx lr
    struct.pack_into('<I', raw, VIEW - 0x10000, HOME)
    return bytes(raw)


def payload(va=PAYLOAD_VA, memsz=0x40):
    bl = 0xeb000000 | (((HOME - (va + 4 + 8)) >> 2) & 0xffffff)
    code = struct.pack('<4I', 0xe52de004, bl, 0xe2800023, 0xe49df004)  # push {lr}; bl home; add r0, #35; pop {pc}
    raw = bytearray(elf_header(va, 1)) + struct.pack('<8I', 1, 0x80, va, va, len(code), memsz, 7, 4)
    raw += bytes(0x80 - len(raw)) + code
    return bytes(raw)


def profile(program):
    return {'input_sha256': hashlib.sha256(program).hexdigest(), 'home_show': HOME, 'view_entry': VIEW}


class PatchTests(unittest.TestCase):
    def test_layout(self):
        program = fixture()
        out = patch.apply(program, payload(), profile(program))
        headers = [h for _, h in patch.program_headers(out)]
        self.assertEqual([h[0] for h in headers], [1, 1, 1])
        kind, off, va, pa, filesz, memsz, flags, align = headers[2]
        self.assertEqual((va, pa, filesz, memsz, flags, align), (PAYLOAD_VA, PAYLOAD_VA, 16, 0x40, 7, 0x10000))
        self.assertEqual(off % 0x10000, va % 0x10000)
        self.assertEqual(out[off:off + 16], payload()[0x80:0x90])
        self.assertEqual(struct.unpack_from('<I', out, VIEW - 0x10000)[0], PAYLOAD_VA)
        self.assertEqual(out[:len(program)][:52], program[:52])  # file header untouched
        self.assertEqual(out, patch.apply(program, payload(), profile(program)))  # deterministic

    def test_refusals(self):
        program = fixture()
        cases = {
            'sha256': (program + b'x', payload(), profile(program)),
            'aligned': (program, payload(va=PAYLOAD_VA + 0x100), profile(program)),
            'overlaps': (program, payload(va=0x20000), profile(program)),
            'Home show': (program, payload(), dict(profile(program), home_show=HOME + 4)),
            'PT_NOTE': (fixture(True), payload(), profile(fixture(True))),
        }
        for words, (prog, pay, prof) in cases.items():
            with self.subTest(words), self.assertRaisesRegex(ValueError, words):
                patch.apply(prog, pay, prof)
        with self.assertRaisesRegex(ValueError, 'one loaded segment'):
            patch.payload_segment(payload()[:52] + bytes(32))

    @unittest.skipUnless(shutil.which('qemu-arm'), 'qemu-arm not installed')
    def test_runs_and_calls_original(self):
        program = fixture()
        with tempfile.TemporaryDirectory() as d:
            for name, data, code in (('stock', program, 7), ('patched', patch.apply(program, payload(), profile(program)), 42)):
                path = os.path.join(d, name)
                with open(path, 'wb') as f:
                    f.write(data)
                os.chmod(path, 0o755)
                self.assertEqual(subprocess.run(['qemu-arm', path]).returncode, code, name)


if __name__ == '__main__':
    unittest.main()
