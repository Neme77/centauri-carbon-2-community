"""ARM instruction tests with OpenSSL/pthread calls stubbed (not firmware execution)."""
from pathlib import Path
from collections import Counter
from unicorn import Uc,UC_ARCH_ARM,UC_MODE_ARM,UC_HOOK_CODE
from unicorn.arm_const import *
b=(Path(__file__).resolve().parents[1]/'dualtrust/daemon-000-dualtrust-v2').read_bytes()
END=0x200000;SP=0x308000;HEADER=0x304000
F={'stock':0x3b3d0,'bio':0x157e4,'pem':0x15a30,'biofree':0x15778,'ctx':0x15934,'init':0x15e38,'sha':0x16048,'ctrl':0x15fa0,'verify':0x15a3c,'ctxfree':0x163e4,'lock':0x15730,'unlock':0x164a4}
cases=[('stock_success',{'stock':0}),('community_success',{}),('signature_bad',{'verify':0}),('verify_error',{'verify':-1}),('bio_failed',{'bio':0}),('pem_failed',{'pem':0}),('ctx_failed',{'ctx':0}),('init_failed',{'init':0}),('ctrl_failed',{'ctrl':0}),('lock_failed',{'lock':1}),('unlock_failed',{'unlock':1}),('repeated_1000',{})]
for name,over in cases:
 vals={'stock':-1,'bio':0x301000,'pem':0x301100,'ctx':0x301200,'init':1,'sha':0x301300,'ctrl':1,'verify':1,'biofree':1,'ctxfree':0,'lock':0,'unlock':0};vals.update(over)
 u=Uc(UC_ARCH_ARM,UC_MODE_ARM);u.mem_map(0x10000,0x100000);u.mem_write(0x10000,b[:0x855c4]);u.mem_map(0x200000,0x1000);u.mem_map(0x300000,0x10000)
 regs=[UC_ARM_REG_R4,UC_ARM_REG_R5,UC_ARM_REG_R6,UC_ARM_REG_R7,UC_ARM_REG_R8,UC_ARM_REG_R9,UC_ARM_REG_R10,UC_ARM_REG_R11]
 calls=[]
 def hook(u,a,size,data):
  if a==END:u.emu_stop();return
  for key,addr in F.items():
   if a==addr:
    calls.append(key);assert u.reg_read(UC_ARM_REG_SP)%8==0
    if key in ('lock','unlock'): assert u.reg_read(UC_ARM_REG_R0)==0xa7eb0
    if key=='ctx': assert u.reg_read(UC_ARM_REG_R0)==vals['pem']
    if key=='bio':
     assert u.reg_read(UC_ARM_REG_R1)==451
     assert bytes(u.mem_read(u.reg_read(UC_ARM_REG_R0),26))==b'-----BEGIN PUBLIC KEY-----'
    if key=='verify':
     assert u.reg_read(UC_ARM_REG_R1)==HEADER+0x100
     assert u.reg_read(UC_ARM_REG_R2)==256
     assert u.reg_read(UC_ARM_REG_R3)==HEADER+0xe0
     assert int.from_bytes(u.mem_read(u.reg_read(UC_ARM_REG_SP),4),'little')==32
    if key=='ctrl':
     assert u.reg_read(UC_ARM_REG_R1)==0xffffffff and u.reg_read(UC_ARM_REG_R2)==0xf8 and u.reg_read(UC_ARM_REG_R3)==1
     assert int.from_bytes(u.mem_read(u.reg_read(UC_ARM_REG_SP)+4,4),'little')==vals['sha']
    u.reg_write(UC_ARM_REG_R0,vals[key]&0xffffffff);u.reg_write(UC_ARM_REG_PC,u.reg_read(UC_ARM_REG_LR));return
 u.hook_add(UC_HOOK_CODE,hook)
 for iteration in range(1000 if name=='repeated_1000' else 1):
  u.reg_write(UC_ARM_REG_SP,SP);u.reg_write(UC_ARM_REG_LR,END);u.reg_write(UC_ARM_REG_R0,HEADER)
  for r in regs:u.reg_write(r,0xabcdef)
  u.emu_start(0x952ac,0,count=400)
  assert u.reg_read(UC_ARM_REG_PC)==END
  expected=0 if name in ['stock_success','community_success','repeated_1000'] else 0xffffffff
  assert u.reg_read(UC_ARM_REG_R0)==expected
  assert u.reg_read(UC_ARM_REG_SP)==SP and all(u.reg_read(r)==0xabcdef for r in regs)
 c=Counter(calls)
 if name=='stock_success': assert calls==['stock']
 else:
  assert c['unlock']==(0 if name=='lock_failed' else c['lock'])
  assert c['biofree']==(c['bio'] if vals['bio'] else 0)
  assert c['ctxfree']==(c['ctx'] if vals['ctx'] else 0)
 if name=='repeated_1000': assert c['pem']==1 and c['ctxfree']==1000 and c['lock']==1000
 print(name,'PASS')
