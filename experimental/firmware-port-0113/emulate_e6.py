#!/usr/bin/env python3
"""E6-Emulation (neuer Build): LanStrategy::stop ruft stop_http_server_file NICHT mehr auf.

Fuehrt LanStrategy::stop (neu 0x826924, 184 B) in Unicorn aus. Alle Aufrufe
AUSSERHALB der Funktion werden abgefangen und kehren sofort zurueck; dabei wird
protokolliert, welche Zieladressen aufgerufen wurden. PASS, wenn
stop_http_server_file (0x827148) NICHT aufgerufen wurde, die Funktion bis zum
Ruecksprung laeuft und mindestens ein anderer Stop-Pfad aufgerufen wird.

Laedt die Binary nur lesend; schreibt NICHTS, kein Geraetezugriff.
Aufruf:  python3 emulate_e6.py WORK/elegoo_printer.k1
"""
import sys
from elftools.elf.elffile import ELFFile
from unicorn import Uc, UC_ARCH_ARM, UC_MODE_ARM, UC_HOOK_CODE
from unicorn.arm_const import (UC_ARM_REG_R0, UC_ARM_REG_R1, UC_ARM_REG_R2,
    UC_ARM_REG_R3, UC_ARM_REG_FP, UC_ARM_REG_SP, UC_ARM_REG_LR, UC_ARM_REG_PC)

path = sys.argv[1] if len(sys.argv) > 1 else "WORK/elegoo_printer.k1"

STOP_FN_START, STOP_FN_LEN = 0x826924, 184          # LanStrategy::stop (neu)
STOP_FN_END = STOP_FN_START + STOP_FN_LEN
STOP_HTTP = 0x827148                                # stop_http_server_file (neu) — darf NICHT kommen

u = Uc(UC_ARCH_ARM, UC_MODE_ARM)
with open(path, "rb") as stream:
    for seg in ELFFile(stream).iter_segments():
        if seg["p_type"] != "PT_LOAD":
            continue
        a = seg["p_vaddr"]; start = a & ~4095
        end = (a + seg["p_memsz"] + 4095) & ~4095
        u.mem_map(start, end - start); u.mem_write(a, seg.data())
obj, stack, sentinel = 0x2000000, 0x2100000, 0x2200000
u.mem_map(obj, 0x2000); u.mem_map(stack, 0x10000); u.mem_map(sentinel, 0x1000)

called = []
def hook(m, address, size, data):
    if STOP_FN_START <= address < STOP_FN_END:
        return                                      # interner Code: normal ausfuehren
    called.append(address)                          # externer Aufruf: protokollieren + zurueck
    m.reg_write(UC_ARM_REG_PC, m.reg_read(UC_ARM_REG_LR))
u.hook_add(UC_HOOK_CODE, hook)

for r in (UC_ARM_REG_R1, UC_ARM_REG_R2, UC_ARM_REG_R3):
    u.reg_write(r, 0)
u.reg_write(UC_ARM_REG_R0, obj)
u.reg_write(UC_ARM_REG_FP, stack + 0x8000)
u.reg_write(UC_ARM_REG_SP, stack + 0x7000)
u.reg_write(UC_ARM_REG_LR, sentinel)
reached_end = True
try:
    u.emu_start(STOP_FN_START, sentinel, count=2000)
except Exception as e:
    reached_end = False
    err = repr(e)

http_called = STOP_HTTP in called
other_calls = [a for a in called if a != STOP_HTTP]
ok = (not http_called) and reached_end and len(other_calls) > 0

print(f"E6 Emulation gegen {path}")
print(f"  stop_http_server_file (0x{STOP_HTTP:x}) aufgerufen? {http_called}   (soll: False)")
print(f"  Funktion bis Ruecksprung gelaufen? {reached_end}   (soll: True)")
print(f"  andere Aufrufe (andere Stop-Pfade): {len(other_calls)}  -> {[hex(a) for a in other_calls[:8]]}")
if not reached_end:
    print("  Hinweis:", err)
print(f"\n{'PASS' if ok else 'FAIL'}: E6 "
      f"({'HTTP-Stop entfaellt, Funktion laeuft, andere Pfade aktiv' if ok else 'Kriterium verletzt'})")
print("LIMIT: Vendor-Funktionen abgefangen; HTTP/Netzwerk + Hardware unverifiziert.")
sys.exit(0 if ok else 1)
