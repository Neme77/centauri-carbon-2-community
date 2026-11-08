#!/usr/bin/env python3
"""E3/E4/E5-Emulation fuer den NEUEN Build (02.01.01.13), angepasst aus
experimental/firmware-port/test_http44_emulation.py (+0x7790-Adressen).

Fuehrt die gepatchten HTTP-Upload-Statuswaechter des Block-Einbaus (0x84f940 ->
neu 0x8570d0) in Unicorn aus und prueft pro Maschinenstatus den Kontrollfluss,
den Member-Offset 0xbbc (vs. Stale 0xbb4) und die HTTP-409-Reklamation.

Laedt die Binary nur lesend in den Emulator; schreibt NICHTS in die Datei, kein
Geraetezugriff. Voraussetzung:  pip install unicorn pyelftools

Aufruf:  python3 emulate_e345.py WORK/elegoo_printer.k1
"""
import sys
from elftools.elf.elffile import ELFFile
from unicorn import Uc, UC_ARCH_ARM, UC_MODE_ARM, UC_HOOK_CODE
from unicorn.arm_const import (UC_ARM_REG_R0, UC_ARM_REG_R1, UC_ARM_REG_R2,
    UC_ARM_REG_R3, UC_ARM_REG_R4, UC_ARM_REG_FP, UC_ARM_REG_SP, UC_ARM_REG_LR, UC_ARM_REG_PC)

path = sys.argv[1] if len(sys.argv) > 1 else "WORK/elegoo_printer.k1"

# --- Adressen, neuer Build (alt + 0x7790) ---
GETINSTANCE = 0x882290          # DeviceStatus::getInstance (war 0x87ab00)
GUARDS = [                      # (entry, erwartetes Ziel, erlaubte Status)
    (0x857100, 0x89625c, {1, 11}),   # set_status(EMachineStatus,EMachineSubStatus)
    (0x857120, 0x895e18, {1, 11}),   # set_status(EMachineStatus,bool)
    (0x857140, 0x896594, {11}),      # set_sub_status
]
CANCEL_ENTRY = 0x857160          # war 0x84f9d0
CANCEL_OK, CANCEL_REJECT = 0x858648, 0x858a70   # war 0x850eb8 / 0x8512e0

u = Uc(UC_ARCH_ARM, UC_MODE_ARM)
with open(path, "rb") as stream:
    elf = ELFFile(stream)
    for seg in elf.iter_segments():
        if seg["p_type"] != "PT_LOAD":
            continue
        a = seg["p_vaddr"]; start = a & ~4095
        end = (a + seg["p_memsz"] + 4095) & ~4095
        u.mem_map(start, end - start)
        u.mem_write(a, seg.data())
object_address, stack, sentinel = 0x2000000, 0x2100000, 0x2200000
u.mem_map(object_address, 0x2000); u.mem_map(stack, 0x10000); u.mem_map(sentinel, 0x1000)

seen = []; stop_at = set()
def hook(m, address, size, data):
    if address == GETINSTANCE:
        m.reg_write(UC_ARM_REG_R0, object_address)
        m.reg_write(UC_ARM_REG_PC, m.reg_read(UC_ARM_REG_LR, UC_ARM_REG_PC))
    elif address in stop_at:
        seen.append(address); m.emu_stop()
u.hook_add(UC_HOOK_CODE, hook)

def run(entry, state, exits):
    seen.clear(); stop_at.clear(); stop_at.update(exits)
    u.mem_write(object_address + 0xbbc, state.to_bytes(4, "little"))
    u.mem_write(object_address + 0xbb4, (11 if state != 11 else 2).to_bytes(4, "little"))
    for r in (UC_ARM_REG_R1, UC_ARM_REG_R2, UC_ARM_REG_R3, UC_ARM_REG_R4):
        u.reg_write(r, 0)
    u.reg_write(UC_ARM_REG_R0, object_address)
    u.reg_write(UC_ARM_REG_FP, stack + 0x8000)
    u.reg_write(UC_ARM_REG_SP, stack + 0x7000)
    u.reg_write(UC_ARM_REG_LR, sentinel)
    u.emu_start(entry, sentinel + 4, count=100)
    if len(seen) != 1:
        return None
    return seen[0]

ok = fail = 0; fails = []
for state in range(32):
    for entry, target, allowed in GUARDS:
        res = run(entry, state, {target, sentinel})
        exp = target if state in allowed else sentinel
        good = (res == exp); ok += good; fail += not good
        if not good: fails.append(f"guard 0x{entry:x} state {state}: {res and hex(res)} != {hex(exp)}")
    res = run(CANCEL_ENTRY, state, {CANCEL_OK, CANCEL_REJECT})
    exp = CANCEL_OK if state in {1, 11} else CANCEL_REJECT
    good = (res == exp)
    if good and state not in {1, 11}:
        good = (u.reg_read(UC_ARM_REG_R4) == 409)
    ok += good; fail += not good
    if not good: fails.append(f"cancel state {state}: res={res and hex(res)} r4={u.reg_read(UC_ARM_REG_R4)}")

print(f"E3/E4/E5 Emulation gegen {path}")
print(f"{ok} PASS, {fail} FAIL  (128 Faelle: 32 Status x 4 Pfade)")
for f in fails[:20]:
    print("  FAIL:", f)
print("PASS-Kriterium: 128 PASS, 0 FAIL.")
print("LIMIT: Vendor-Funktionen abgefangen; HTTP/Netzwerk + Hardware unverifiziert.")
sys.exit(1 if fail else 0)
