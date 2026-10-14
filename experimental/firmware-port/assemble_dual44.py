#!/usr/bin/env python3
"""Reproduce the pinned 4.4 wrapper; no vendor binary or private key required."""
from pathlib import Path
import hashlib,json
from keystone import Ks,KS_ARCH_ARM,KS_MODE_ARM
root=Path(__file__).resolve().parent
m=json.loads((root/'dual44-manifest.json').read_text())
s=(root/'dual_verify_44.S').read_text()
s='\n'.join(line.split('//')[0] for line in s.splitlines())
for directive in ['.syntax unified','.arm','.section .text','.global dual_verify_wrapper']:s=s.replace(directive,'')
pem=(root/'cc2_community_release_public.pem').read_bytes()
if len(pem)!=451:raise SystemExit('Public key length mismatch')
s=s.replace('.incbin "cc2_community_release_public.pem"','.byte '+','.join(map(str,pem)))
b=bytes(Ks(KS_ARCH_ARM,KS_MODE_ARM).asm(s,addr=m['payload_va'])[0])
if len(b)!=m['payload_size'] or hashlib.sha256(b).hexdigest()!=m['payload_sha256']:raise SystemExit('Assembly mismatch; output not replaced')
(root/'dual_verify_44.bin').write_bytes(b)
print('ASSEMBLY PASS',len(b),hashlib.sha256(b).hexdigest())
