#!/usr/bin/env python3
"""Execute real constructor callsites and formatter gate; vendor string ctor mocked."""
from pathlib import Path
import sys
from elftools.elf.elffile import ELFFile
from unicorn import Uc, UC_ARCH_ARM, UC_MODE_ARM, UC_HOOK_CODE
from unicorn.arm_const import *
u=Uc(UC_ARCH_ARM,UC_MODE_ARM)
with Path(sys.argv[1]).open('rb') as f:
 e=ELFFile(f)
 for s in e.iter_segments():
  if s['p_type']=='PT_LOAD':
   start=s['p_vaddr']&~4095;end=(s['p_vaddr']+s['p_memsz']+4095)&~4095
   u.mem_map(start,end-start);u.mem_write(s['p_vaddr'],s.data())
mem=0x2000000;u.mem_map(mem,0x40000);owner=mem+0x1000;frame=mem+0x20000;captured=[];exits=set();cancelled=[];found=False
regs=[UC_ARM_REG_R0,UC_ARM_REG_R1,UC_ARM_REG_R2,UC_ARM_REG_R4,UC_ARM_REG_R5,UC_ARM_REG_R6,UC_ARM_REG_R7,UC_ARM_REG_R8,UC_ARM_REG_R9,UC_ARM_REG_R10]
def write(a,v):u.mem_write(a,v.to_bytes(4,'little'))
def text(a):
 b=bytearray()
 while True:
  c=u.mem_read(a+len(b),1)
  if c==b'\0':return b.decode()
  b.extend(c)
def hook(m,a,z,data):
 if a in exits:m.emu_stop()
 elif a==0xbf2c8:
  captured.append(text(m.reg_read(UC_ARM_REG_R1)));m.reg_write(UC_ARM_REG_PC,m.reg_read(UC_ARM_REG_LR))
 # Native TimerManager lookup/duplicate branch, with only STL operations mocked.
 elif a in [0x942300,0x942334,0x942360,0xcf64c,0xca108,0xc3c40,0x1baaf8,0x9423a4,0x940078,0x9423d4]:
  if a==0x942360:m.reg_write(UC_ARM_REG_R0,int(found))
  elif a==0x9423a4:m.reg_write(UC_ARM_REG_R0,owner+0x400)
  elif a==0x940078:cancelled.append(m.reg_read(UC_ARM_REG_R2))
  else:m.reg_write(UC_ARM_REG_R0,owner)
  m.reg_write(UC_ARM_REG_PC,m.reg_read(UC_ARM_REG_LR))
u.hook_add(UC_HOOK_CODE,hook)
count=0;names={}
for mode in [0,1,2,3]:
 for entry,default in [(0x837168,0xbabddc),(0x8371a0,0xbabdec)]:
  captured.clear();exits.clear();exits.add(entry+4)
  u.reg_write(UC_ARM_REG_FP,frame);u.reg_write(UC_ARM_REG_SP,frame-0x1000)
  write(frame-0x68,owner);write(owner+0x104,mode)
  expected_regs={r:mem+0x100+r for r in regs}
  for r,v in expected_regs.items():u.reg_write(r,v)
  u.reg_write(UC_ARM_REG_R1,default)
  u.emu_start(entry,entry+4,count=100)
  assert len(captured)==1
  expected=('CC2_LAN_'+text(default)) if mode==1 else text(default)
  assert captured[0]==expected,(mode,entry,captured,expected)
  for r,v in expected_regs.items():
   if r!=UC_ARM_REG_R1:assert u.reg_read(r)==v,(mode,r)
  assert u.reg_read(UC_ARM_REG_SP)==frame-0x1000
  names[mode,entry]=captured[0];count+=1
# Starting LAN after WAN must not replace either native WAN timer key.
for entry in [0x837168,0x8371a0]:assert names[1,entry]!=names[2,entry]
# Enter actual TimerManager duplicate decision: same key cancels existing task;
# distinct key leaves it alone. Native containers are mocked, cancellation is not.
for found in [False,True]:
 exits.clear();exits.add(0x93d5dc);cancelled.clear()
 u.reg_write(UC_ARM_REG_FP,frame);u.reg_write(UC_ARM_REG_SP,frame-0x1000)
 write(frame-0x108,owner);write(frame+4,owner+0x800);write(owner+0x418,123);write(owner+0x41c,0)
 u.emu_start(0x93d4e0,0x93d5dc,count=1000)
 assert cancelled==([123] if found else []),cancelled;count+=1
for mode in [0,1,2,3]:
 exits.clear();exits.update([0x88db60,0x88dd74]);u.reg_write(UC_ARM_REG_R3,mode)
 u.emu_start(0x88db48,0x88dd78,count=20)
 assert u.reg_read(UC_ARM_REG_PC)==(0x88db60 if mode<=1 else 0x88dd74);count+=1
print(f'PASS: {count} ARM cases: constructor ABI, distinct LAN/WAN timer keys, native duplicate cancellation, dual formatter gate')
print('LIMIT: MQTT broker and cloud pairing require printer validation')
