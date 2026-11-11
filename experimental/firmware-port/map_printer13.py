"""Inventory .13 function and call-site candidates without patching the ELF."""

import argparse
import hashlib
import json
from pathlib import Path
import struct


HERE = Path(__file__).resolve().parent
STOCK_HASHES = (
    '8039081f9c4b01e720d838d4ab6e4515a8979b9a71a33bd4164321c282e06074',
    'f0111e0b52c2ea39ff2aae70675c53b311f7340df8ed233e16e8e0cc9f883e14',
)


def functions(data):
    header = struct.unpack_from('<16sHHIIIIIHHHHHH', data)
    sections = [struct.unpack_from('<10I', data, header[6] + i * header[11])
                for i in range(header[12])]
    symbols = next(section for section in sections if section[1] == 2)
    strings = sections[symbols[6]][4]
    result = {}
    for offset in range(symbols[4], symbols[4] + symbols[5], symbols[9]):
        name, va, size, info, _, index = struct.unpack_from('<IIIBBH', data, offset)
        if info & 15 != 2 or not size or not 0 < index < len(sections):
            continue
        section = sections[index]
        start = section[4] + (va & ~1) - section[3]
        name += strings
        result[data[name:data.index(0, name)].decode()] = (va, data[start:start + size])
    return result


def site(old, new, va, size):
    owners = [(len(body), name, start, body) for name, (start, body) in old.items()
              if not start & 1 and start <= va and va + size <= start + len(body)]
    if not owners:
        return {'status': 'outside ARM function'}
    _, name, start, body = min(owners)
    if name not in new:
        return {'symbol': name, 'status': 'symbol absent'}
    next_start, next_body = new[name]
    delta = va - start
    # Exact local context provides a candidate site, not a relocated branch.
    left, right = max(0, delta - 12), min(len(body), delta + size + 12)
    context = body[left:right]
    match = next_body.find(context)
    unique = match >= 0 and next_body.find(context, match + 1) < 0
    return {'symbol': name, 'function_13': hex(next_start),
            'function_size_07': len(body), 'function_size_13': len(next_body),
            'site_13': hex(next_start + match + delta - left) if unique else None,
            'status': 'exact context' if unique else 'review instruction layout'}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('stock07', type=Path)
    parser.add_argument('stock13', type=Path)
    args = parser.parse_args()
    binaries = [path.read_bytes() for path in (args.stock07, args.stock13)]
    if tuple(hashlib.sha256(data).hexdigest() for data in binaries) != STOCK_HASHES:
        raise ValueError('Unrecognized stock printer binary')
    old, new = map(functions, binaries)
    report = {}
    for name in ('http44', 'network44'):
        patches = json.loads((HERE / (name + '-manifest.json')).read_text())['patches']
        report[name] = [
            {'site_07': patch.get('va'), 'reason': patch['reason'],
             **(site(old, new, int(patch['va'], 16), len(bytes.fromhex(patch['before'])))
                if 'va' in patch else {'status': 'review ELF layout'})}
            for patch in patches
        ]
    reactor = json.loads((HERE / 'reactor/port-manifest.json').read_text())
    report['reactor'] = [
        {'site_07': address, **site(old, new, int(address, 16), 4)}
        for address in reactor['mapping'].values()
    ]
    print(json.dumps(report, indent=2))


if __name__ == '__main__':
    main()
