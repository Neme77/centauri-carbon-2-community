#!/usr/bin/env python3
"""Targeted regression test for the 2026-11-08 real-hardware crash.

Starts emulation directly at payload offset 0x114 (VA 0xcc50c0, `bl
get_mode()`), the two-instruction sequence right before the bug: `bl
get_mode` -> `cmp r0,#0` -> `bne <target>` at offset 0x11c. In the first
corrected build that `bne` branched to a stale 02.01.01.07-era target
(garbage code at 0x87b538) instead of the correct
DeviceStatus::post_change_message+0x304 (0x895504). That stale branch is the
most plausible cause of the SIGSEGV (pc=0) reported after real-hardware
testing: `lr` at the time of the fault matched exactly the return address of
this `bl get_mode`, and the instruction right after it (0x11c) was the first
one never relocated by the original porting pass.

(Starting here directly, rather than from the payload's dispatcher entry at
offset 0xe4, is deliberate: that dispatcher branches to different vendor
functions depending on object state not relevant to this specific bug, the
same way emulate_e6.py/emulate_e345.py start at their specific guard entries
rather than replaying an entire call chain.)

PASS only if execution reaches 0x895504 (the corrected target) and NOT the
previous garbage target 0x87b538. `NetworkManager::get_mode()` is forced to
return non-zero (LAN/WAN mode active), since the crash report correlated the
fault with "RTC WAN streaming enabled" -- i.e. this test exercises exactly
the branch that is taken only in that state.

Loads the binary read-only; writes NOTHING, no device access.
Aufruf:  python3 emulate_crash_20261108.py WORK/corrected_v2/elegoo_printer.patched
"""
import sys
from elftools.elf.elffile import ELFFile
from unicorn import Uc, UC_ARCH_ARM, UC_MODE_ARM, UC_HOOK_CODE
from unicorn.arm_const import (UC_ARM_REG_R0, UC_ARM_REG_FP, UC_ARM_REG_SP,
    UC_ARM_REG_LR, UC_ARM_REG_PC)

path = sys.argv[1] if len(sys.argv) > 1 else "WORK/corrected_v2/elegoo_printer.patched"

ENTRY = 0xCC50C0                 # payload offset 0x114: bl get_mode()
GET_MODE = 0x819C30              # NetworkManager::get_mode() -- intercepted, forced non-zero
FIXED_TARGET = 0x895504          # post_change_message+0x304 -- the CORRECT target
STALE_TARGET = 0x87B538          # the bug's garbage target in the FIRST corrected build
OBJ = 0x2000000

u = Uc(UC_ARCH_ARM, UC_MODE_ARM)
with open(path, "rb") as stream:
    for seg in ELFFile(stream).iter_segments():
        if seg["p_type"] != "PT_LOAD":
            continue
        a = seg["p_vaddr"]; lo = a & ~4095; hi = (a + seg["p_memsz"] + 4095) & ~4095
        u.mem_map(lo, hi - lo); u.mem_write(a, seg.data())
stack, sentinel = 0x2100000, 0x2200000
u.mem_map(OBJ, 0x2000); u.mem_map(stack, 0x10000); u.mem_map(sentinel, 0x1000)

reached = []
def hook(m, address, size, data):
    if address == GET_MODE:
        m.reg_write(UC_ARM_REG_R0, 1)  # force "mode active" (LAN/WAN), as in the crash
        m.reg_write(UC_ARM_REG_PC, m.reg_read(UC_ARM_REG_LR))
        return
    if address in (FIXED_TARGET, STALE_TARGET, sentinel):
        reached.append(address)
        m.emu_stop()
u.hook_add(UC_HOOK_CODE, hook)

u.reg_write(UC_ARM_REG_R0, OBJ)
u.reg_write(UC_ARM_REG_FP, stack + 0x8000)
u.reg_write(UC_ARM_REG_SP, stack + 0x7000)
u.reg_write(UC_ARM_REG_LR, sentinel)
try:
    u.emu_start(ENTRY, sentinel, count=20)
except Exception as e:
    print("Emulation exception:", repr(e))

hit = reached[0] if reached else None
ok = (hit == FIXED_TARGET)

print(f"Crash-Regressionstest gegen {path}")
print(f"  Einstieg 0x{ENTRY:x} (bl get_mode), get_mode() auf 'aktiv' (1) gezwungen")
print(f"  erreichtes Ziel: {hex(hit) if hit else None}")
print(f"  korrektes Ziel (0x{FIXED_TARGET:x})? {hit == FIXED_TARGET}")
print(f"  altes defektes Ziel (0x{STALE_TARGET:x})? {hit == STALE_TARGET}")
print(f"\n{'PASS' if ok else 'FAIL'}: Absturzpfad vom 2026-11-08 "
      f"({'korrigiert, erreicht den richtigen Call' if ok else 'NICHT korrigiert -- Bug weiterhin vorhanden!'})")
print("LIMIT: nur Kontrollfluss an dieser einen Stelle; Hardware-Bestaetigung bleibt noetig.")
sys.exit(0 if ok else 1)
