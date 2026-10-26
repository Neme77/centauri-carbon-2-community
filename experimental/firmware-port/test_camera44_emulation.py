#!/usr/bin/env python3
"""Execute native camera mode branches; mock JSON/STL and video I/O only."""
import hashlib,json,struct,sys
from pathlib import Path
from elftools.elf.elffile import ELFFile
from unicorn import Uc,UC_ARCH_ARM,UC_MODE_ARM,UC_HOOK_CODE
from unicorn.arm_const import *
port=Path(__file__).parent
camera,printer=map(Path,sys.argv[1:])
c=json.loads((port/'camera44-manifest.json').read_text());n=json.loads((port/'network44-manifest.json').read_text())
assert hashlib.sha256(camera.read_bytes()).hexdigest()==c['output_sha256']
assert hashlib.sha256(printer.read_bytes()).hexdigest()==n['output_sha256']
# WAN startup must retain native stop/pause, which enables new WAN RTC.
o=0x81d6d0-0x10000;word=struct.unpack('<I',printer.read_bytes()[o:o+4])[0]
d=word&0xffffff;d=d-(1<<24) if d&(1<<23) else d
assert 0x81d6d0+8+d*4==0x7d2170
u=Uc(UC_ARCH_ARM,UC_MODE_ARM)
with camera.open('rb') as f:
 e=ELFFile(f)
 for s in e.iter_segments():
  if s['p_type']=='PT_LOAD':
   start=s['p_vaddr']&~4095;end=(s['p_vaddr']+s['p_memsz']+4095)&~4095;u.mem_map(start,end-start);u.mem_write(s['p_vaddr'],s.data())
mem=0x3000000;u.mem_map(mem,0x40000);frame=mem+0x20000;rtc=mem+0x3000;obj=mem+0x4000
calls=[];pause=False;setter_lr=0
def hook(m,a,size,data):
 global setter_lr
 if a==0x16b820:setter_lr=m.reg_read(UC_ARM_REG_LR)
 if a in [0x129858,0x1298e4]:m.emu_stop();return
 if a==0x16b85c:
  # Native SetWanStreamEnabled just wrote this+0x80, no synthetic state update.
  calls.append(('wan',m.reg_read(UC_ARM_REG_R2)));m.reg_write(UC_ARM_REG_SP,m.reg_read(UC_ARM_REG_FP)-8);saved=m.mem_read(m.reg_read(UC_ARM_REG_SP),12);r4,fp,lr=struct.unpack('<III',saved);m.reg_write(UC_ARM_REG_R4,r4);m.reg_write(UC_ARM_REG_FP,fp);m.reg_write(UC_ARM_REG_SP,m.reg_read(UC_ARM_REG_SP)+12);m.reg_write(UC_ARM_REG_PC,setter_lr);return
 stubs={0xf0848:obj,0x12d7e8:1 if pause else 0,0x12d020:1,0x12dfcc:rtc,0x12dff4:obj}
 # For the resume case first comparison is false, second true.
 if a==0x12d7e8 and m.reg_read(UC_ARM_REG_R1)==0x1bc3cc:stubs[a]=0 if pause else 1
 if a in [0x163300,0x1633a0]:calls.append(('video','pause' if a==0x163300 else 'resume'));stubs[a]=0
 if a in [0x88b2c,0x7c2d4,0x7c0a0,0x154f3c,0x7bfa4,0x7c658]:stubs[a]=0
 if a in stubs:m.reg_write(UC_ARM_REG_R0,stubs[a]);m.reg_write(UC_ARM_REG_PC,m.reg_read(UC_ARM_REG_LR))
u.hook_add(UC_HOOK_CODE,hook)
count=0
for pause in [True,False,True,False]:
 for initial in [0,1]:
  calls.clear();u.mem_write(rtc+0x80,bytes([initial]));u.mem_write(frame-0xa0,struct.pack('<I',obj));u.mem_write(frame-0xa4,struct.pack('<I',obj));u.mem_write(frame-0xa8,struct.pack('<I',obj))
  u.reg_write(UC_ARM_REG_FP,frame);u.reg_write(UC_ARM_REG_SP,frame-0x1000)
  u.emu_start(0x129784,0x129a6c,count=500)
  assert calls==([('video','resume'),('wan',1)] if pause else [('wan',0),('video','resume')]),calls
  assert u.mem_read(rtc+0x80,1)==bytes([1 if pause else 0])
  count+=1
print(f'PASS: {count} native ARM camera mode branches/transitions: WAN RTC enabled + local video; LAN RTC disabled + local video')
print('LIMIT: JSON/STL, mutex and video I/O mocked; actual camera capture/remote RTC require printer testing.')
