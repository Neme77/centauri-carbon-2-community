"""Build an offline .13 GUI candidate with the native network page extension."""

import argparse
import hashlib
import json
from pathlib import Path
import struct

from audit_stock13 import imports, functions
from elf_layout import expand_rx, reserve_bss

HERE = Path(__file__).resolve().parent
PALETTE13 = '75dbbd81ed9bf29337e42dbbcec58baea1018250038961d4ef6fbd81906efbab'


def encode_branch(word, source, target):
    delta = target - source - 8
    if word & 0x0e000000 != 0x0a000000 or delta % 4 or not -33554432 <= delta < 33554432:
        raise ValueError('Invalid ARM branch relocation')
    return (word & 0xff000000) | ((delta // 4) & 0xffffff)


def branch_target(word, source):
    return source + 8 + 4 * (((word & 0xffffff) ^ 0x800000) - 0x800000)


def imm16(word):
    return ((word >> 4) & 0xf000) | (word & 0xfff)


def relocate(old, new):
    if hashlib.sha256(old).hexdigest() != '24d0e561287a4b5a87c8ec698c98f60acf2c70511114b36224b4eb9249283abd':
        raise ValueError('Unsupported stock .07 GUI')
    if hashlib.sha256(new).hexdigest() != PALETTE13:
        raise ValueError('Unsupported palette .13 GUI')
    proof = json.loads((HERE / 'network-proof.json').read_text())
    plan = json.loads((HERE / 'patch-plan.json').read_text())
    base, code_end = proof['added_range']
    entry, = (entry for entry in plan['changes'] if entry['offset'] == base - 0x10000)
    payload = bytearray.fromhex(entry['data'])
    old_functions, new_functions = map(functions, (old, new))
    old_imports, new_imports = map(imports, (old, new))
    new_base = 0x2219f0
    _, state = reserve_bss(expand_rx(new, payload), proof['state_bytes'])
    calls = {int(row['site'], 16): row['function'] for row in proof['calls']}
    for offset in range(0, code_end - base, 4):
        word, = struct.unpack_from('<I', payload, offset)
        address = base + offset
        if word & 0x0e000000 == 0x0a000000:
            target = branch_target(word, address)
            if base <= target < code_end:
                continue
            name = calls.pop(address)
            before = old_functions[name][0] if name in old_functions else old_imports[name]
            after = new_functions[name][0] if name in new_functions else new_imports[name]
            if target != before:
                raise ValueError('Unexpected native call target: ' + name)
            struct.pack_into('<I', payload, offset, encode_branch(word, new_base + offset, after))
        elif word & 0x0ff00000 == 0x03000000:
            high, = struct.unpack_from('<I', payload, offset + 4)
            if high & 0x0ff0f000 != 0x03400000 | (word & 0xf000):
                continue
            value = imm16(word) | (imm16(high) << 16)
            if base <= value < base + len(payload):
                value += new_base - base
            elif value == proof['state_address']:
                value = state
            elif value == 0x25aa54:
                value = 0x25bc8c  # setting_wlan_main_menu in the exact .13 symbol table
            else:
                continue
            for delta, instruction, immediate in ((0, word, value & 0xffff), (4, high, value >> 16)):
                struct.pack_into('<I', payload, offset + delta,
                                 (instruction & ~0xf0fff) | ((immediate & 0xf000) << 4) | (immediate & 0xfff))
    if calls:
        raise ValueError('Unprocessed native calls')
    result, _ = reserve_bss(expand_rx(new, payload), proof['state_bytes'])
    result = bytearray(result)
    for row, owner, label in zip(proof['original_sites'],
                                 ('setting_wlan_view_create', 'ui_view_set_wlan_show'),
                                 ('constructor_tail', 'show_tail'), strict=True):
        source = new_functions[owner][0] + row['address'] - old_functions[owner][0]
        offset = source - 0x10000
        original, = struct.unpack_from('<I', result, offset)
        expected = bytes.fromhex(row['before'])
        if label == 'constructor_tail':
            if result[offset:offset + 4] != expected:
                raise ValueError('Changed native network constructor return')
        elif (original & 0xff000000 != 0xea000000
              or branch_target(original, source) != new_functions['lv_obj_clear_flag'][0]):
            raise ValueError('Changed native network show path')
        target = new_base + proof['labels'][label] - base
        struct.pack_into('<I', result, offset, encode_branch(0xea000000, source, target))
    return bytes(result)


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ('stock07', 'palette13', 'output'):
        parser.add_argument(name, type=Path)
    args = parser.parse_args()
    output = relocate(args.stock07.read_bytes(), args.palette13.read_bytes())
    with args.output.open('xb') as file:
        file.write(output)
    print('DISPLAY NETWORK TRIAL', hashlib.sha256(output).hexdigest())
