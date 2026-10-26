#!/usr/bin/env python3
"""Ported v1 token fallback: real ARM callsites, mocked libc/string calls.

Checks native empty-token branches separately, both overloads, WAN-only
fallback, disabled protection, empty stored credential and callee-save ABI.
This does not validate live Matrix/cloud association or the HTTP transport.
"""
import hashlib,json,struct,sys
from pathlib import Path
from elftools.elf.elffile import ELFFile
from unicorn import Uc,UC_ARCH_ARM,UC_MODE_ARM,UC_HOOK_CODE
from unicorn.arm_const import *
p=Path(sys.argv[1]);data=p.read_bytes();manifest=json.loads(Path(__file__).with_name('network44-manifest.json').read_text())
assert hashlib.sha256(data).hexdigest()==manifest['output_sha256']
# Assembly and pinned bytes must agree; no stale helper source is shipped.
from keystone import Ks,KS_ARCH_ARM,KS_MODE_ARM
import re
fallback=manifest['http_token_fallback'];va=int(fallback['va'],16)
assembly=Path(__file__).with_name(fallback['source']).read_text()
code=bytes(Ks(KS_ARCH_ARM,KS_MODE_ARM).asm(re.sub(r'/\*.*?\*/','',assembly,flags=re.S),addr=va)[0])
assert data[va-65536:va-65536+len(code)]==code
u=Uc(UC_ARCH_ARM,UC_MODE_ARM)
with p.open('rb') as f:
 for s in ELFFile(f).iter_segments():
  if s['p_type']=='PT_LOAD':
   start=s['p_vaddr']&~4095;end=(s['p_vaddr']+s['p_memsz']+4095)&~4095;u.mem_map(start,end-start);u.mem_write(s['p_vaddr'],s.data())
mem=0x2000000;u.mem_map(mem,0x40000);state=mem+0x1000;request=mem+0x4000;expected=mem+0x5000;saved=mem+0x6000;stack=mem+0x20000
stop=set();empty=False

def readstr(a):
 out=b''
 for _ in range(128):
  x=bytes(u.mem_read(a,1));a+=1
  if x==b'\0':return out
  out+=x
 raise AssertionError('Unbounded string')

def hook(m,a,size,_):
 if a in stop:m.emu_stop();return
 value=None
 if a==0xc0090:value=0 if readstr(m.reg_read(UC_ARM_REG_R0))==readstr(m.reg_read(UC_ARM_REG_R1)) else 1
 elif a==0x87ab00:value=state
 elif a==0xbf7f0:value=struct.unpack('<I',m.mem_read(m.reg_read(UC_ARM_REG_R0),4))[0]
 elif a==0xbed4c:value=int(empty)
 elif a==0xc006c:value=0 # request GetParam mocked only for the empty guard
 if value is not None:
  m.reg_write(UC_ARM_REG_R0,value);m.reg_write(UC_ARM_REG_PC,m.reg_read(UC_ARM_REG_LR))
u.hook_add(UC_HOOK_CODE,hook)
count=0
for call in [0x851754,0x851924]:
 for lan in [0,1]:
  for enabled in [0,1]:
   for req,active,stored,accepted in [('active','active','local',True),('wrong','active','local',False),('local','active','local',not lan and bool(enabled)),('local','active','',False)]:
    for addr,text in [(request,req),(expected,active),(saved,stored)]:u.mem_write(addr,text.encode()+b'\0')
    u.mem_write(state+0xd90,bytes([lan,enabled]));u.mem_write(state+0xd94,struct.pack('<II',saved,len(stored)))
    u.reg_write(UC_ARM_REG_R0,request);u.reg_write(UC_ARM_REG_R1,expected);u.reg_write(UC_ARM_REG_SP,stack)
    regs=[UC_ARM_REG_R4,UC_ARM_REG_R5,UC_ARM_REG_R6,UC_ARM_REG_R7,UC_ARM_REG_R8,UC_ARM_REG_R9,UC_ARM_REG_R10,UC_ARM_REG_FP]
    for i,r in enumerate(regs):u.reg_write(r,0x123400+i)
    stop.clear();stop.add(call+4);u.emu_start(call,call+4,count=200)
    assert u.reg_read(UC_ARM_REG_PC)==call+4
    assert (u.reg_read(UC_ARM_REG_R0)==0)==accepted,(call,lan,enabled,req,stored)
    assert u.reg_read(UC_ARM_REG_SP)==stack
    assert [u.reg_read(r) for r in regs]==list(range(0x123400,0x123400+len(regs)))
    count+=1
# Execute native request-empty guards: stop on either rejected logging path
# or the first credential lookup. Empty input never reaches the fallback.
for entry,reject,lookup in [(0x8515d8,0x851620,0x851698),(0x85183c,0x851864,0x8518dc)]:
 for empty in [True,False]:
  u.reg_write(UC_ARM_REG_SP,stack);u.reg_write(UC_ARM_REG_R0,state);u.reg_write(UC_ARM_REG_R1,request)
  stop.clear();stop.update([reject,lookup]);u.emu_start(entry,mem+0x30000,count=100)
  assert u.reg_read(UC_ARM_REG_PC)==(reject if empty else lookup)
  count+=1
print(f'PASS: {count} ARM HTTP token cases; both real callsites, native empty guards and preserved registers/stack')
print('LIMIT: libc/string and request extraction mocked; printer WAN association requires hardware testing')
