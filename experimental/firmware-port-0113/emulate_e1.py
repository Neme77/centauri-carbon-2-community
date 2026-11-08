#!/usr/bin/env python3
"""E1 (GUI): Z-Offset-Menue unabhaengig vom Druckstatus erreichbar.

Startet an der gepatchten Case-5-Stelle (0x747c8) und verfolgt den PC-Pfad:
muss die Z-Offset-Stelle (0x74bd0) erreichen und data_get_print_status (0xae640)
NICHT aufrufen. Laedt die Binary nur lesend; schreibt nichts, kein Geraetezugriff.
Aufruf:  python3 emulate_e1.py WORK/ec-eeb001-gui.patched
"""
import sys
from elftools.elf.elffile import ELFFile
from unicorn import Uc, UC_ARCH_ARM, UC_MODE_ARM, UC_HOOK_CODE
from unicorn.arm_const import (UC_ARM_REG_R0, UC_ARM_REG_FP, UC_ARM_REG_SP, UC_ARM_REG_LR, UC_ARM_REG_PC)
path = sys.argv[1] if len(sys.argv) > 1 else "WORK/ec-eeb001-gui.patched"
PATCH, ZOFF, STATUS = 0x747c8, 0x74bd0, 0xae640
u = Uc(UC_ARCH_ARM, UC_MODE_ARM)
with open(path, "rb") as s:
    for seg in ELFFile(s).iter_segments():
        if seg["p_type"] != "PT_LOAD": continue
        a = seg["p_vaddr"]; lo = a & ~4095; hi = (a + seg["p_memsz"] + 4095) & ~4095
        u.mem_map(lo, hi - lo); u.mem_write(a, seg.data())
stack, sentinel = 0x2100000, 0x2200000
u.mem_map(stack, 0x10000); u.mem_map(sentinel, 0x1000)
trace = []
def hook(m, address, size, data):
    trace.append(address)
    if address == STATUS or address == ZOFF:
        # nach Erreichen des Z-Offset-Epilogs: Ausfuehrung beenden, sobald die Funktion verlassen wird
        if address == ZOFF: m.reg_write(UC_ARM_REG_LR, sentinel)
u.hook_add(UC_HOOK_CODE, hook)
u.reg_write(UC_ARM_REG_R0, 0); u.reg_write(UC_ARM_REG_FP, stack + 0x8000)
u.reg_write(UC_ARM_REG_SP, stack + 0x7000); u.reg_write(UC_ARM_REG_LR, sentinel)
try:
    u.emu_start(PATCH, sentinel, count=40)
except Exception:
    pass
reached = ZOFF in trace
status = STATUS in trace
ok = reached and not status
print(f"E1 Emulation gegen {path}")
print(f"  Z-Offset-Pfad (0x{ZOFF:x}) erreicht? {reached}   (soll True)")
print(f"  data_get_print_status (0x{STATUS:x}) aufgerufen? {status}   (soll False)")
print(f"  PC-Pfad (Anfang): {[hex(x) for x in trace[:6]]}")
print(f"\n{'PASS' if ok else 'FAIL'}: E1 (Z-Offset-Menue ohne Druckstatus-Gate)")
print("LIMIT: nur Kontrollfluss; UI/Hardware unverifiziert.")
sys.exit(0 if ok else 1)
