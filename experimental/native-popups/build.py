# SPDX-License-Identifier: GPL-3.0-or-later
"""Build the patched screen program from an owner-supplied V4.4.3 screen program.

usage: python3 build.py --gui /private/ec-eeb001-gui --out /private/native-popups

The vendor program is only read with nm and objdump, never executed. The payload
(nui.c) is compiled for the printer, its undefined names are resolved to the
program's own LVGL functions and PLT entries, and patch.py appends it. The output
directory gets ec-eeb001-gui, nui.elf and manifest.json; it is vendor-derived
and stays private.
"""
import argparse
import hashlib
import json
import os
import re
import subprocess
import sys
from pathlib import Path

import patch

HERE = Path(__file__).resolve().parent
CFLAGS = ['-marm', '-march=armv7-a', '-mfloat-abi=hard', '-mfpu=vfpv3-d16', '-O2', '-std=c11',
          '-Wall', '-Wextra', '-Werror', '-ffreestanding', '-fno-pic', '-fno-stack-protector',
          '-fno-builtin', '-ffunction-sections', '-fdata-sections', '-mno-unaligned-access']


def run(*args, **kw):
    return subprocess.run(args, check=True, capture_output=True, text=True, **kw).stdout


def program_symbols(cross, gui):
    """Defined functions by name, then PLT entries by imported name."""
    names = {}
    for line in run(cross + 'nm', '--defined-only', str(gui)).splitlines():
        parts = line.split()
        if len(parts) == 3 and parts[1] in 'Tt':
            names.setdefault(parts[2], int(parts[0], 16))
    for m in re.finditer(r'^([0-9a-f]+) <([A-Za-z_0-9]+)@plt>:', run(cross + 'objdump', '-d', '-j', '.plt', str(gui)), re.M):
        names.setdefault(m.group(2), int(m.group(1), 16))
    return names


def resolve(undefined, names):
    missing = [n for n in undefined if n not in names]
    if missing:
        raise SystemExit('not in the screen program: ' + ' '.join(missing))
    return ''.join(f'{n} = 0x{names[n]:08x};\n' for n in undefined)


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument('--gui', required=True, type=Path, help='stock V4.4.3 /opt/bin/ec-eeb001-gui')
    ap.add_argument('--out', required=True, type=Path)
    a = ap.parse_args()
    cross = os.environ.get('CROSS', 'arm-linux-gnueabihf-')
    program = a.gui.read_bytes()
    if hashlib.sha256(program).hexdigest() != patch.V443['input_sha256']:
        raise SystemExit('unsupported screen program; only the CC2 Community V4.4.3 one is accepted')
    a.out.mkdir(parents=True, exist_ok=True)
    obj, elf, syms = a.out / 'nui.o', a.out / 'nui.elf', a.out / 'syms.ld'
    run(cross + 'gcc', *CFLAGS, '-c', str(HERE / 'nui.c'), '-o', str(obj))
    undefined = [l.split()[-1] for l in run(cross + 'nm', '-u', str(obj)).splitlines()
                 if not l.split()[-1].startswith(('__aeabi', '__gnu'))]
    syms.write_text(resolve(undefined, program_symbols(cross, a.gui)))
    run(cross + 'gcc', '-nostdlib', '-static', '-Wl,-T,' + str(HERE / 'link.ld'), '-Wl,-T,' + str(syms),
        '-Wl,--no-dynamic-linker', '-Wl,-N', '-Wl,--no-warn-rwx-segments', '-o', str(elf), str(obj), '-lgcc')
    out = patch.apply(program, elf.read_bytes())
    (a.out / 'ec-eeb001-gui').write_bytes(out)
    manifest = {
        'schema': 1,
        'input_sha256': patch.V443['input_sha256'],
        'output_sha256': hashlib.sha256(out).hexdigest(),
        'payload_sha256': hashlib.sha256(elf.read_bytes()).hexdigest(),
    }
    (a.out / 'manifest.json').write_text(json.dumps(manifest, indent=2) + '\n')
    print(json.dumps(manifest, indent=2))


if __name__ == '__main__':
    sys.exit(main())
