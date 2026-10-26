from pathlib import Path
import sys,json,struct
from elftools.elf.elffile import ELFFile
from capstone import Cs,CS_ARCH_ARM,CS_MODE_ARM
from unicorn import Uc,UC_ARCH_ARM,UC_MODE_ARM,UC_HOOK_CODE
from unicorn.arm_const import UC_ARM_REG_R0,UC_ARM_REG_R1,UC_ARM_REG_LR,UC_ARM_REG_PC,UC_ARM_REG_SP
import argparse,hashlib
parser=argparse.ArgumentParser(description='Local-only native constructor regression; vendor executable is never bundled.')
parser.add_argument('camera',type=Path)
args=parser.parse_args()
assert hashlib.sha256(args.camera.read_bytes()).hexdigest()=='ad888fec26ce214427eda038ae70b76cf68155f365c669ed647c1c09bad7a1f7'
def execute(site):
 u=Uc(UC_ARCH_ARM,UC_MODE_ARM)
 with args.camera.open('rb') as f:
  e=ELFFile(f)
  for s in e.iter_segments():
   if s['p_type']=='PT_LOAD':
    start=s['p_vaddr']&~4095;end=(s['p_vaddr']+s['p_memsz']+4095)&~4095
    u.mem_map(start,end-start);u.mem_write(s['p_vaddr'],s.data())
 obj=0x3000000;stack=0x4000000;sentinel=0x5000000
 for a in (obj,stack,sentinel):u.mem_map(a,0x10000)
 u.reg_write(UC_ARM_REG_R0,obj);u.reg_write(UC_ARM_REG_R1,obj+0x1000)
 u.reg_write(UC_ARM_REG_SP,stack+0x8000);u.reg_write(UC_ARM_REG_LR,sentinel)
 md=Cs(CS_ARCH_ARM,CS_MODE_ARM);calls=[]
 def hook(uc,address,size,data):
  ins=next(md.disasm(bytes(uc.mem_read(address,size)),address))
  if ins.mnemonic!='bl':return
  calls.append(hex(address));a=uc.reg_read(UC_ARM_REG_R0)
  target=int(ins.op_str.lstrip('#'),16)
  if address==site:uc.mem_write(a,b'\1')
  elif target==0x165654:uc.mem_write(a,b'\0')
  elif target in (0x7de94,0x7ded0):uc.mem_write(a,bytes([uc.reg_read(UC_ARM_REG_R1)&255]))
  uc.reg_write(UC_ARM_REG_PC,address+4)
 u.hook_add(UC_HOOK_CODE,hook)
 u.emu_start(0x162e0c,sentinel,count=10000)
 return {'enabled_after_full_constructor':int(u.mem_read(obj+0x19,1)[0]),'calls':calls}
r={label:execute(site) for label,site in [('original',None),('R1',0x162e60),('R2',0x162f28)]}
assert r['original']['enabled_after_full_constructor']==0
assert r['R1']['enabled_after_full_constructor']==0
assert r['R2']['enabled_after_full_constructor']==1
r['limits']='Complete native constructor control flow; external constructors and atomic calls mocked. No camera, SDK or network executed.'

print(json.dumps(r,indent=2))
