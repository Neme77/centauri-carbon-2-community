#!/usr/bin/env python3
"""Legacy coexistence helper relocation tests, with vendor calls mocked."""
import hashlib
import json
from pathlib import Path
import sys
from elftools.elf.elffile import ELFFile
from unicorn import Uc,UC_ARCH_ARM,UC_MODE_ARM,UC_HOOK_CODE
from unicorn.arm_const import *
path=Path(sys.argv[1]);manifest=json.loads(Path(__file__).with_name('network44-manifest.json').read_text());assert hashlib.sha256(path.read_bytes()).hexdigest()==manifest['output_sha256']
u=Uc(UC_ARCH_ARM,UC_MODE_ARM)
with path.open('rb') as stream:
 e=ELFFile(stream);symbols={s.name:s['st_value'] for s in e.get_section_by_name('.symtab').iter_symbols()}
 for s in e.iter_segments():
  if s['p_type']=='PT_LOAD':
   start=s['p_vaddr']&~4095;end=(s['p_vaddr']+s['p_memsz']+4095)&~4095;u.mem_map(start,end-start);u.mem_write(s['p_vaddr'],s.data())
base=int(manifest['payload_va'],16);mem=0x2000000;u.mem_map(mem,0x40000);owner=mem+0x1000;lan=mem+0x2000;wan=mem+0x3000;frame=mem+0x20000;sentinel=mem+0x30000
calls=[];exits=set()
get=symbols['_ZN13communication14NetworkManager12get_instanceEv'];lanstart=symbols['_ZN13communication11LanStrategy5startEv'];lanstop=symbols['_ZN13communication11LanStrategy16stop_mqtt_clientEv'];lanreport=symbols['_ZN13communication11LanStrategy14report_messageERKNSt7__cxx1112basic_stringIcSt11char_traitsIcESaIcEEE']
def write(address,value):u.mem_write(address,value.to_bytes(4,'little'))
def hook(m,a,size,data):
 if a in exits:m.emu_stop()
 elif a==get:m.reg_write(UC_ARM_REG_R0,owner);m.reg_write(UC_ARM_REG_PC,m.reg_read(UC_ARM_REG_LR))
 elif a in {lanstart,lanstop,lanreport,sentinel+4}:
  calls.append((a,m.reg_read(UC_ARM_REG_R0)));m.reg_write(UC_ARM_REG_PC,m.reg_read(UC_ARM_REG_LR))
u.hook_add(UC_HOOK_CODE,hook)
def reset():
 calls.clear();u.reg_write(UC_ARM_REG_FP,frame);u.reg_write(UC_ARM_REG_SP,frame-0x1000);u.reg_write(UC_ARM_REG_LR,sentinel);exits.clear();exits.add(sentinel)
 for reg in [UC_ARM_REG_R0,UC_ARM_REG_R1,UC_ARM_REG_R2,UC_ARM_REG_R3,UC_ARM_REG_R4,UC_ARM_REG_R5,UC_ARM_REG_R6]:u.reg_write(reg,0)
count=0
for mode in range(4):
 for present in [False,True]:
  for enabled in [False,True]:
   for client in [False,True]:
    reset();write(frame-0x1a8,owner);write(owner+0x40,mode);write(owner+8,lan if present else 0);u.mem_write(lan+0x5d,bytes([enabled]));write(lan+0xc,1 if client else 0);exits.add(0x812498)
    u.emu_start(base,sentinel+8,count=100)
    assert calls==([(lanstart,lan)] if mode==1 and present and not enabled and client else []),(mode,present,enabled,client,calls)
    count+=1
for present in [False,True]:
 for enabled in [False,True]:
  for same in [False,True]:
   reset();write(owner+8,lan if present else 0);u.mem_write(lan+0x5d,bytes([enabled]));active=lan if same else wan;u.reg_write(UC_ARM_REG_R1,owner);u.reg_write(UC_ARM_REG_R2,active);u.reg_write(UC_ARM_REG_R3,sentinel+4)
   u.emu_start(base+0x5c,sentinel+8,count=100)
   assert calls==([(lanstop,lan)] if present and enabled and not same else [])+[(sentinel+4,active)],calls
   assert u.reg_read(UC_ARM_REG_SP)==frame-0x1000
   count+=1
# Enter the actual patched callsites with r1 deliberately invalid. The legacy
# helper requires the owner in r1: preceding setup must load it from the frame.
for entry,slot,exit_pc in [(0x811c00,8,0x811c14),(0x812114,0x1a8,0x812128)]:
 reset();write(frame-slot,owner);write(owner+8,lan);u.mem_write(lan+0x5d,b'\x01')
 table=mem+0x4000;write(wan,table);write(table+0x10,sentinel+4)
 u.reg_write(UC_ARM_REG_R2,wan);u.reg_write(UC_ARM_REG_R1,0xdead0000)
 exits.add(exit_pc);u.emu_start(entry,sentinel+8,count=100)
 assert calls==[(lanstop,lan),(sentinel+4,wan)],calls
 assert u.reg_read(UC_ARM_REG_SP)==frame-0x1000
 count+=1
print(f'PASS: {count} ARM LAN/WAN cases, including real teardown callsites with poisoned r1')
print('LIMIT: mocked vendor calls; MQTT, webcam and actual mode switching require printer validation')
