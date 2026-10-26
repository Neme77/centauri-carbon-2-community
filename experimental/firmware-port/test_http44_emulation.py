#!/usr/bin/env python3
"""Execute actual draft ARM guard instructions; vendor functions are intercepted."""
import hashlib
import json
from pathlib import Path
import sys
from elftools.elf.elffile import ELFFile
from unicorn import Uc, UC_ARCH_ARM, UC_MODE_ARM, UC_HOOK_CODE
from unicorn.arm_const import UC_ARM_REG_R0, UC_ARM_REG_R1, UC_ARM_REG_R2, UC_ARM_REG_R3, UC_ARM_REG_R4, UC_ARM_REG_FP, UC_ARM_REG_SP, UC_ARM_REG_LR, UC_ARM_REG_PC

path = Path(sys.argv[1])
manifest = json.loads(Path(__file__).with_name('http44-manifest.json').read_text())
assert hashlib.sha256(path.read_bytes()).hexdigest() == manifest['output_sha256']
u = Uc(UC_ARCH_ARM, UC_MODE_ARM)
with path.open('rb') as stream:
    elf = ELFFile(stream)
    for segment in elf.iter_segments():
        if segment['p_type'] != 'PT_LOAD':
            continue
        address = segment['p_vaddr']
        start = address & ~4095
        end = (address + segment['p_memsz'] + 4095) & ~4095
        u.mem_map(start, end - start)
        u.mem_write(address, segment.data())
object_address = 0x2000000
stack = 0x2100000
sentinel = 0x2200000
u.mem_map(object_address, 0x2000)
u.mem_map(stack, 0x10000)
u.mem_map(sentinel, 0x1000)
seen = []
stop_at = set()
def hook(machine, address, size, data):
    if address == 0x87ab00:  # DeviceStatus::getInstance
        machine.reg_write(UC_ARM_REG_R0, object_address)
        machine.reg_write(UC_ARM_REG_PC, machine.reg_read(UC_ARM_REG_LR))
    elif address in stop_at:
        seen.append(address)
        machine.emu_stop()
u.hook_add(UC_HOOK_CODE, hook)

def run(entry, state, exits):
    seen.clear()
    stop_at.clear()
    stop_at.update(exits)
    u.mem_write(object_address + 0xbbc, state.to_bytes(4, 'little'))
    # Deliberately different stale offset: detects accidentally retaining the old layout.
    u.mem_write(object_address + 0xbb4, (11 if state != 11 else 2).to_bytes(4, 'little'))
    for register in [UC_ARM_REG_R1, UC_ARM_REG_R2, UC_ARM_REG_R3, UC_ARM_REG_R4]:
        u.reg_write(register, 0)
    u.reg_write(UC_ARM_REG_R0, object_address)
    u.reg_write(UC_ARM_REG_FP, stack + 0x8000)
    u.reg_write(UC_ARM_REG_SP, stack + 0x7000)
    u.reg_write(UC_ARM_REG_LR, sentinel)
    u.emu_start(entry, sentinel + 4, count=100)
    assert len(seen) == 1, (hex(entry), state, seen)
    return seen[0]

cases = 0
for state in range(32):
    for entry, target, allowed in [
        (0x84f970, 0x88eacc, {1, 11}),
        (0x84f990, 0x88e688, {1, 11}),
        (0x84f9b0, 0x88ee04, {11}),
    ]:
        result = run(entry, state, {target, sentinel})
        assert result == (target if state in allowed else sentinel), (hex(entry), state)
        cases += 1
    result = run(0x84f9d0, state, {0x850eb8, 0x8512e0})
    assert result == (0x850eb8 if state in {1, 11} else 0x8512e0), state
    if state not in {1, 11}:
        assert u.reg_read(UC_ARM_REG_R4) == 409
    cases += 1
print(f'PASS: {cases} ARM state-guard cases, including printing, cancellation and changed member offset')
print('LIMIT: vendor functions intercepted; HTTP/network and hardware integration unverified')
