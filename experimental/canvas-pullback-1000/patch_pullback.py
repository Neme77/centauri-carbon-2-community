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


def transform(data, reverse=False):
    source_hash, target_hash = (EXTENDED_SHA256, ORIGINAL_SHA256) if reverse else (ORIGINAL_SHA256, EXTENDED_SHA256)
    before, after = (EXTENDED, ORIGINAL) if reverse else (ORIGINAL, EXTENDED)
    if len(data) != SIZE or hashlib.sha256(data).hexdigest() != source_hash:
        raise ValueError('Unsupported input: exact qualified executable required')
    if data[OFFSET:OFFSET + 8] != before:
        raise ValueError('Pull-back constant differs from the qualified input')
    result = data[:OFFSET] + after + data[OFFSET + 8:]
    if hashlib.sha256(result).hexdigest() != target_hash:
        raise ValueError('Output fingerprint mismatch')
    return result


def apply(source, destination, reverse=False):
    result = transform(Path(source).read_bytes(), reverse)
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
    args = parser.parse_args()
    print(apply(args.source, args.destination, args.reverse))


if __name__ == '__main__':
    main()
