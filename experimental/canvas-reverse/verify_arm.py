#!/usr/bin/env python3
"""Reproduce ARM comparison tests using a recognized original library."""
import argparse
import hashlib
import json
from pathlib import Path
import struct
from capstone import Cs, CS_ARCH_ARM, CS_MODE_ARM
from unicorn import Uc, UC_ARCH_ARM, UC_MODE_ARM
from unicorn.arm_const import UC_ARM_REG_C1_C0_2, UC_ARM_REG_FPEXC, UC_ARM_REG_D7, UC_ARM_REG_FP, UC_ARM_REG_R3, UC_ARM_REG_PC

def emulate(code, address, distance, movement):
    uc = Uc(UC_ARCH_ARM, UC_MODE_ARM)
    uc.mem_map(address & ~0xfff, 0x2000)
    uc.mem_write(address, code)
    uc.mem_map(0x10000, 0x2000)
    uc.reg_write(UC_ARM_REG_FP, 0x11000)
    uc.mem_write(0x11000-0x124, struct.pack('<i', distance))
    uc.reg_write(UC_ARM_REG_D7, struct.unpack('<Q', struct.pack('<d', movement))[0])
    uc.reg_write(UC_ARM_REG_C1_C0_2, 0xf << 20)
    uc.reg_write(UC_ARM_REG_FPEXC, 0x40000000)
    uc.emu_start(address, 0, count=11)
    return uc.reg_read(UC_ARM_REG_R3), uc.reg_read(UC_ARM_REG_PC)

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--library', required=True, type=Path)
    args = parser.parse_args()
    raw = args.library.read_bytes()
    manifest = json.loads(Path(__file__).with_name('manifest.json').read_text())
    matches = [x for x in manifest['libraries'] if x['input_sha256'] == hashlib.sha256(raw).hexdigest()]
    if not matches:
        parser.error('unknown input library')
    x = matches[0]
    off = x['file_offset']
    assert raw[off:off+24].hex() == x['original_hex']
    old = raw[off-16:off+28]
    new = old[:16] + bytes.fromhex(x['patched_hex']) + old[40:]
    start = x['virtual_address']-16
    instructions = list(Cs(CS_ARCH_ARM, CS_MODE_ARM).disasm(old,start))
    assert len(instructions) == 11
    loop_target = int(instructions[-1].op_str.lstrip('#'),16)
    assert emulate(old,start,-10,0)[0] == 1
    count = 0
    for distance in (-1400,-600,-10,-1,1,10,600,1400):
        target = abs(distance)
        for magnitude in (0,target/2,target,target+0.25,target*1.1):
            for sign in (-1,1):
                movement = magnitude*sign
                got = emulate(new,start,distance,movement)
                expected = int(abs(movement)>target)
                assert got == (expected,start+44 if expected else loop_target)
                if distance>0:
                    assert got == emulate(old,start,distance,movement)
                count += 1
    print(json.dumps({'compatible_versions':[x['ota_version'] for x in matches],'cases_passed':count,'original_bug_reproduced':True,'forward_behaviour_preserved':True,'branch_destinations_verified':True}))

if __name__ == '__main__':
    main()
