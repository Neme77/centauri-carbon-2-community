#!/usr/bin/env python3
"""Reproduce the pinned 02.01.01.07 wrapper with GNU ARM binutils."""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parent
DEFAULT_PREFIX = '/opt/cc2-cross/toolchain-out/bin/arm-cortex_a15-linux-gnueabihf'


def assemble(prefix):
    manifest = json.loads((ROOT / 'dual44-manifest.json').read_text(encoding='utf-8'))
    with tempfile.TemporaryDirectory(prefix='cc2-dual44-') as temporary:
        directory = Path(temporary)
        obj, elf, binary = (directory / name for name in ('wrapper.o', 'wrapper.elf', 'wrapper.bin'))
        subprocess.run([prefix + '-as', '-march=armv7-a', '-o', str(obj), 'dual_verify_44.S'], cwd=ROOT, check=True)
        subprocess.run([prefix + '-ld', '-Ttext', hex(manifest['payload_va']), '-e', 'dual_verify_wrapper',
                        '-o', str(elf), str(obj)], check=True)
        subprocess.run([prefix + '-objcopy', '-O', 'binary', '-j', '.text', str(elf), str(binary)], check=True)
        data = binary.read_bytes()
    digest = hashlib.sha256(data).hexdigest()
    if len(data) != manifest['payload_size'] or digest != manifest['payload_sha256']:
        raise RuntimeError('ARM wrapper differs from the qualified payload; output not replaced')
    return data


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--prefix', default=DEFAULT_PREFIX)
    args = parser.parse_args()
    data = assemble(args.prefix)
    (ROOT / 'dual_verify_44.bin').write_bytes(data)
    print('ASSEMBLY PASS', len(data), hashlib.sha256(data).hexdigest())


if __name__ == '__main__':
    main()
