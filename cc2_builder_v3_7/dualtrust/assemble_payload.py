#!/usr/bin/env python3
"""Reproduce ARM payload using keystone-engine. No private keys required."""
from pathlib import Path
from keystone import Ks,KS_ARCH_ARM,KS_MODE_ARM
import hashlib
r=Path(__file__).resolve().parent
s=(r/'dual_verify.S').read_text()
s='\n'.join(line.split('//')[0] for line in s.splitlines())
s=s.replace('.syntax unified','').replace('.arm','').replace('.section .text','').replace('.global dual_verify_wrapper','')
pem=(r/'cc2_community_release_public.pem').read_bytes()
if len(pem)!=451:raise SystemExit('Unexpected embedded PEM length')
s=s.replace('.incbin "cc2_community_release_public.pem"','.byte '+','.join(map(str,pem)))
b=bytes(Ks(KS_ARCH_ARM,KS_MODE_ARM).asm(s,addr=0x952ac)[0])
expected='33d013ca4a63334fdbbd0adb77bd10f81e2e21c3ac41229e216eb17e5949d06f'
if hashlib.sha256(b).hexdigest()!=expected: raise SystemExit('Assembly differs from validated v2 payload; output not replaced')
(r/'dual_verify.bin').write_bytes(b)
print(len(b),hashlib.sha256(b).hexdigest())
