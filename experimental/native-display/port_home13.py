"""Combine the .13 network and Home extensions in a local GUI trial."""

import argparse
import hashlib
import json
from pathlib import Path
import struct

from elf_layout import reserve_bss
from port_network13 import HERE, branch_target, encode_branch, functions, imm16, imports, relocate


def build(stock07, palette13):
    network = relocate(stock07, palette13)
    result, state = reserve_bss(network, 32)
    result = bytearray(result)
    proof = json.loads((HERE / 'home-proof.json').read_text())
    plan = json.loads((HERE / 'patch-plan.json').read_text())
    base, end = proof['added_range']
    rows = plan['changes']
    payload = bytearray.fromhex(next(row['data'] for row in rows if row['offset'] == base - 0x10000))
    old, new = map(functions, (stock07, palette13))
    old_plt, new_plt = map(imports, (stock07, palette13))
    rx_header = 52 + 3 * 32
    rx = struct.unpack_from('<8I', result, rx_header)
    if rx[:3] != (1, 0, 0x10000) or rx[4] != rx[5] or rx[6] != 5:
        raise ValueError('Unexpected executable segment')
    insert = rx[4]
    new_base = rx[2] + insert
    if new_base + len(payload) >= 0x231000 or any(result[insert:insert + len(payload)]):
        raise ValueError('Home payload does not fit the reserved RX extension')
    calls = {int(row['site'], 16): row['function'] for row in proof['calls']}
    resumes = {}
    for name, (address, word) in proof['original_entries'].items():
        relocated = new[name][0]
        slot = proof['labels']['trampoline_' + name] - base
        if struct.unpack_from('<I', payload, slot)[0] != word:
            raise ValueError('Unexpected trampoline instruction')
        payload[slot:slot + 4] = palette13[relocated - 0x10000:relocated - 0xfffc]
        resumes[address + 4] = relocated + 4
    for offset in range(0, end - base, 4):
        word, = struct.unpack_from('<I', payload, offset)
        address = base + offset
        if word & 0x0e000000 == 0x0a000000:
            target = branch_target(word, address)
            if base <= target < end:
                continue
            if address in calls:
                name = calls.pop(address)
                before = old[name][0] if name in old else old_plt[name]
                after = new[name][0] if name in new else new_plt[name]
                if target != before:
                    raise ValueError('Unexpected Home call target: ' + name)
            else:
                after = resumes[target]
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
            elif (value in (0x259590, 0x259b04) or 0x259698 <= value < 0x259708
                  or 0x250634 <= value < 0x250774):
                value += 0x1230
            else:
                continue
            for delta, instruction, immediate in ((0, word, value & 0xffff), (4, high, value >> 16)):
                struct.pack_into('<I', payload, offset + delta,
                                 (instruction & ~0xf0fff) | ((immediate & 0xf000) << 4) | (immediate & 0xfff))
    if calls:
        raise ValueError('Unprocessed Home calls')
    for row in rows:
        offset, length = row['offset'], row['length']
        replacement = bytes.fromhex(row['data'])
        if 0x240638 <= offset <= 0x240750:
            offset += 0x1230
        elif 0x4c000 <= offset < 0x70000:
            address = offset + 0x10000
            word, = struct.unpack_from('<I', replacement)
            if word & 0x0e000000 == 0x0a000000:
                target = branch_target(word, address)
                if not base <= target < end:
                    raise ValueError('Unexpected Home hook destination')
                original, = struct.unpack_from('<I', result, offset)
                if address in (0x5c2b4, 0x5c304):
                    if branch_target(original, address) != new['lv_img_set_src'][0]:
                        raise ValueError('Changed native LED call')
                elif address == 0x5c5a8:
                    if original != 0xe30a37c0:
                        raise ValueError('Changed Home hide prologue')
                elif hashlib.sha256(result[offset:offset + length]).hexdigest() != row['before_sha256']:
                    raise ValueError('Changed Home hook instruction')
                replacement = struct.pack('<I', encode_branch(word, address, new_base + target - base))
                result[offset:offset + length] = replacement
                continue
        else:
            continue
        if hashlib.sha256(result[offset:offset + length]).hexdigest() != row['before_sha256']:
            raise ValueError('Changed Home data or layout instruction')
        result[offset:offset + length] = replacement
    result[insert:insert + len(payload)] = payload
    struct.pack_into('<II', result, rx_header + 16, insert + len(payload), insert + len(payload))
    return bytes(result)


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ('stock07', 'palette13', 'output'):
        parser.add_argument(name, type=Path)
    args = parser.parse_args()
    data = build(args.stock07.read_bytes(), args.palette13.read_bytes())
    with args.output.open('xb') as file:
        file.write(data)
    print('DISPLAY HOME + NETWORK TRIAL', hashlib.sha256(data).hexdigest())
