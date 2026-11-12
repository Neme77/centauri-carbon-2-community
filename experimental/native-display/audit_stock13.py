"""Audit stock .13 display layout and named dependencies without producing a GUI."""

import argparse
import hashlib
import json
from pathlib import Path
import struct
import sys

from elf_layout import expand_rx, reserve_bss

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE.parent / 'firmware-port'))
from map_printer13 import functions, site


def imports(data):
    header = struct.unpack_from('<16sHHIIIIIHHHHHH', data)
    sections = [struct.unpack_from('<10I', data, header[6] + i * header[11])
                for i in range(header[12])]

    def name(base, offset):
        start = base + offset
        return data[start:data.index(0, start)].decode()

    named = {name(sections[header[13]][4], section[0]): section for section in sections}
    plt, rel, symbols, strings = (named[key] for key in ('.plt', '.rel.plt', '.dynsym', '.dynstr'))
    result = {}
    for index in range(rel[5] // 8):
        _, info = struct.unpack_from('<II', data, rel[4] + index * 8)
        offset, = struct.unpack_from('<I', data, symbols[4] + (info >> 8) * 16)
        result[name(strings[4], offset)] = plt[3] + 20 + 12 * index
    return result


def audit(old_data, new_data):
    digests = tuple(hashlib.sha256(data).hexdigest() for data in (old_data, new_data))
    if digests != (
        '24d0e561287a4b5a87c8ec698c98f60acf2c70511114b36224b4eb9249283abd',
        '8d6cac2aecb26b0e504b9d1b71a8b5bb9168fc14143eb0e8fcfc5d7d8f2507fc',
    ):
        raise ValueError('Unrecognized stock GUI')
    old, new = map(functions, (old_data, new_data))
    old_imports, new_imports = map(imports, (old_data, new_data))
    home = json.loads((HERE / 'home-proof.json').read_text())
    network = json.loads((HERE / 'network-proof.json').read_text())
    targets = {}
    for name, address in home['functions'].items():
        if name in old and old[name][0] == address and name in new:
            targets[name] = {'address_13': hex(new[name][0]), 'source': 'function'}
        elif old_imports.get(name) == address and name in new_imports:
            targets[name] = {'address_13': hex(new_imports[name]), 'source': 'PLT'}
        else:
            targets[name] = {'address_13': None, 'source': 'review'}
    sites = [*map(lambda entry: entry[0], home['original_entries'].values()),
             *map(lambda address: int(address, 16), home['original_constructor_return_sites']),
             *(entry['address'] for entry in network['original_sites'])]
    hooks = [{'address_07': hex(address), **site(old, new, address, 4)} for address in sites]
    header = struct.unpack_from('<16sHHIIIIIHHHHHH', new_data)
    segments = [struct.unpack_from('<8I', new_data, header[5] + i * header[9])
                for i in range(header[10])]
    rx, = (segment for segment in segments if segment[0] == 1 and segment[6] == 5)
    payload_size = max(home['added_range'][0] + home['added_bytes'],
                       network['added_range'][0] + network['added_bytes']) - home['added_range'][0]
    _, state = reserve_bss(expand_rx(new_data, bytes(payload_size)), 32 + network['state_bytes'])
    return {'payload_size': payload_size, 'payload_address_13': hex(rx[2] + rx[4]),
            'state_address_13': hex(state), 'home_targets': targets, 'hooks': hooks}


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('stock07', type=Path)
    parser.add_argument('stock13', type=Path)
    args = parser.parse_args()
    print(json.dumps(audit(args.stock07.read_bytes(), args.stock13.read_bytes()), indent=2))
