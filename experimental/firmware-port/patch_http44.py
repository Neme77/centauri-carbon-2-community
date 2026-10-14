#!/usr/bin/env python3
"""Offline draft only: apply reviewed, byte-pinned HTTP changes to the new vendor ELF."""
import hashlib
import json
from pathlib import Path
import sys

def apply(source, destination):
    manifest = json.loads(Path(__file__).with_name('http44-manifest.json').read_text())
    data = bytearray(Path(source).read_bytes())
    if hashlib.sha256(data).hexdigest() != manifest['baseline_sha256']:
        raise ValueError('Unrecognized vendor executable')
    if Path(destination).exists():
        raise ValueError('Output already exists')
    for patch in manifest['patches']:
        offset = patch['offset']
        before, after = bytes.fromhex(patch['before']), bytes.fromhex(patch['after'])
        if len(before) != len(after) or data[offset:offset + len(before)] != before:
            raise ValueError('Patch bytes differ from audited input')
        data[offset:offset + len(before)] = after
    if hashlib.sha256(data).hexdigest() != manifest['output_sha256']:
        raise ValueError('Unexpected patched output')
    Path(destination).write_bytes(data)
    return manifest['output_sha256']

if __name__ == '__main__':
    if len(sys.argv) != 3:
        sys.exit('Usage: patch_http44.py INPUT_VENDOR NEW_OUTPUT')
    print(apply(sys.argv[1], sys.argv[2]))
