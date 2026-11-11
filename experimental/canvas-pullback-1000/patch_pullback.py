#!/usr/bin/env python3
"""Offline-only, fingerprint-pinned reproduction of Noahwhite2014's #172 patch.

Never install, restart a process, or edit a printer configuration from this tool.
"""
import argparse
import hashlib
from pathlib import Path

ORIGINAL_SHA256 = '19712b5943b10bb8cb6aab9a4ed498ee962b28538b23c91b7ed003eb9e590745'
EXTENDED_SHA256 = '15ea20771b1424560bfa0c6e4170993e1ce20001b90715d2b00f57dfcb0ea1bc'
OFFSET = 0x5BBA38
ORIGINAL = bytes.fromhex('0000000000e07540')
EXTENDED = bytes.fromhex('0000000000408f40')
SIZE = 29274740
PROFILE_13 = (
    'ee48e7ee3bb5c6bbd2f6ea780e372f168ed9bd5a3f85eb5cf3f70e726271ba0e',
    '768a9e8c307d2398dc486be3a3ca8dfa6e9265677627d133f03b6914a1a34e59',
    0x5BE4B8, 29229556,
)


def transform(data, reverse=False, *, stock13=False):
    original, extended, offset, size = (
        PROFILE_13 if stock13 else (ORIGINAL_SHA256, EXTENDED_SHA256, OFFSET, SIZE)
    )
    source_hash, target_hash = (extended, original) if reverse else (original, extended)
    before, after = (EXTENDED, ORIGINAL) if reverse else (ORIGINAL, EXTENDED)
    if len(data) != size or hashlib.sha256(data).hexdigest() != source_hash:
        raise ValueError('Unsupported input: exact qualified executable required')
    if data[offset:offset + 8] != before:
        raise ValueError('Pull-back constant differs from the qualified input')
    result = data[:offset] + after + data[offset + 8:]
    if hashlib.sha256(result).hexdigest() != target_hash:
        raise ValueError('Output fingerprint mismatch')
    return result


def apply(source, destination, reverse=False, *, stock13=False):
    result = transform(Path(source).read_bytes(), reverse, stock13=stock13)
    # Exclusive creation also refuses existing paths and symlinks. The caller
    # chooses an offline output; no original executable is ever replaced.
    with Path(destination).open('xb') as output:
        output.write(result)
    return hashlib.sha256(result).hexdigest()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('source', type=Path)
    parser.add_argument('destination', type=Path)
    parser.add_argument('--reverse', action='store_true', help='Reproduce original bytes offline, NOT a printer rollback')
    parser.add_argument('--stock13', action='store_true', help='Use the exact .13 HTTP/MQTT trial executable')
    args = parser.parse_args()
    print(apply(args.source, args.destination, args.reverse, stock13=args.stock13))


if __name__ == '__main__':
    main()
