#!/usr/bin/env python3
"""Check a known CC2 library, or write a separate minimally patched copy."""
import argparse
import hashlib
import json
from pathlib import Path
import sys

def digest(data):
    return hashlib.sha256(data).hexdigest()

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--input', required=True, type=Path, help='Local original libelegoo_extras.so')
    parser.add_argument('--apply', action='store_true', help='Write a new output file; otherwise check only')
    parser.add_argument('--output', type=Path, help='New output path; must not already exist')
    parser.add_argument('--version', help='Optional exact OTA version to require')
    args = parser.parse_args()
    if args.apply != (args.output is not None):
        parser.error('--apply and --output must be supplied together')
    manifest = json.loads(Path(__file__).with_name('manifest.json').read_text())
    raw = args.input.read_bytes()
    sha = digest(raw)
    known = manifest['libraries']
    matching = [x for x in known if x['input_sha256'] == sha]
    fixed = [x for x in known if x['output_sha256'] == sha]
    if not matching:
        if fixed:
            versions = ', '.join(x['ota_version'] for x in fixed)
            print('Input already contains this correction; compatible package(s): ' + versions)
            if args.apply:
                parser.error('already patched; no output written')
            return 0
        parser.error('unknown input SHA-256; refusing to patch: ' + sha)
    if args.version:
        matching = [x for x in matching if x['ota_version'] == args.version]
        if not matching:
            parser.error('input does not match the required OTA version')
    item = matching[0]
    offset = item['file_offset']
    before = bytes.fromhex(item['original_hex'])
    after = bytes.fromhex(item['patched_hex'])
    if len(before) != 24 or len(after) != 24 or raw[offset:offset+24] != before:
        parser.error('instruction precondition failed; refusing to patch')
    patched = raw[:offset] + after + raw[offset+24:]
    if digest(patched) != item['output_sha256']:
        parser.error('expected output checksum mismatch; refusing to write')
    print('Recognized original library: ' + ', '.join(x['ota_version'] for x in matching))
    print('Original SHA-256: ' + sha)
    print('Patched SHA-256:  ' + item['output_sha256'])
    if not args.apply:
        print('Check only; no files changed.')
        return 0
    if args.output.resolve() == args.input.resolve():
        parser.error('output must be separate from input')
    if args.output.exists():
        parser.error('output already exists; refusing to overwrite')
    with args.output.open('xb') as output:
        if output.write(patched) != len(patched):
            raise OSError('Incomplete output write')
    if digest(args.output.read_bytes()) != item['output_sha256']:
        raise OSError('Written output checksum mismatch')
    print('Wrote separately verified file: ' + str(args.output))
    print('No printer files or firmware were installed.')
    return 0

if __name__ == '__main__':
    try:
        sys.exit(main())
    except (OSError, ValueError, KeyError) as error:
        print('Patch failed: ' + str(error), file=sys.stderr)
        sys.exit(1)
