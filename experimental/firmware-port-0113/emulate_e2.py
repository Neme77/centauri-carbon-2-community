#!/usr/bin/env python3
"""E2 (Kamera): LocalRtcMonitor ruft resume statt pause.

Startet an der gepatchten Stelle (0x13287c) und prueft, dass der erste Aufruf
resume (0x16a7f8) trifft, nicht pause (0x16a748). Nur lesend; kein Geraetezugriff.
Aufruf:  python3 emulate_e2.py WORK/ai_camera.patched
"""
import sys
from elftools.elf.elffile import ELFFile
from unicorn import Uc, UC_ARCH_ARM, UC_MODE_ARM, UC_HOOK_CODE
from unicorn.arm_const import (UC_ARM_REG_R0, UC_ARM_REG_FP, UC_ARM_REG_SP, UC_ARM_REG_LR, UC_ARM_REG_PC)
path = sys.argv[1] if len(sys.argv) > 1 else "WORK/ai_camera.patched"
PATCH, RESUME, PAUSE = 0x13287c, 0x16a7f8, 0x16a748
u = Uc(UC_ARCH_ARM, UC_MODE_ARM)
with open(path, "rb") as s:
    for seg in ELFFile(s).iter_segments():
        if seg["p_type"] != "PT_LOAD": continue
        a = seg["p_vaddr"]; lo = a & ~4095; hi = (a + seg["p_memsz"] + 4095) & ~4095
        u.mem_map(lo, hi - lo); u.mem_write(a, seg.data())
stack, sentinel = 0x2100000, 0x2200000
u.mem_map(stack, 0x10000); u.mem_map(sentinel, 0x1000)
first_call = [None]
def hook(m, address, size, data):
    if address == PATCH: return               # das bl selbst ausfuehren
    if first_call[0] is None:
        first_call[0] = address; m.emu_stop()
u.hook_add(UC_HOOK_CODE, hook)
u.reg_write(UC_ARM_REG_R0, 0); u.reg_write(UC_ARM_REG_FP, stack + 0x8000)
u.reg_write(UC_ARM_REG_SP, stack + 0x7000); u.reg_write(UC_ARM_REG_LR, sentinel)
try:
    u.emu_start(PATCH, sentinel, count=10)
except Exception:
    pass
fc = first_call[0]
ok = (fc == RESUME)
print(f"E2 Emulation gegen {path}")
print(f"  erster Aufruf: {fc and hex(fc)}  (resume 0x{RESUME:x} soll, pause 0x{PAUSE:x} NICHT)")
print(f"\n{'PASS' if ok else 'FAIL'}: E2 (Aufruf landet bei resume)")
print("LIMIT: nur Kontrollfluss; Koexistenz/Hardware unverifiziert.")
sys.exit(0 if ok else 1)
