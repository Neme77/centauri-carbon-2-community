#!/usr/bin/env python3
"""Apply the Z menu patch to an exact stock GUI, offline only."""
from pathlib import Path
import hashlib,sys
STOCK='24d0e561287a4b5a87c8ec698c98f60acf2c70511114b36224b4eb9249283abd'
OFFSET=0x652d0
ORIGINAL=bytes.fromhex('a8e400eb')
PATCHED=bytes.fromhex('000100ea')
STOCK_13='8d6cac2aecb26b0e504b9d1b71a8b5bb9168fc14143eb0e8fcfc5d7d8f2507fc'
PROFILES={
 STOCK:(OFFSET,ORIGINAL,PATCHED),
 STOCK_13:(0x647c8,bytes.fromhex('9ce700eb'),PATCHED),
}

def patch(data):
 profile=PROFILES.get(hashlib.sha256(data).hexdigest())
 if profile is None:raise ValueError('Unsupported GUI: SHA256 mismatch')
 offset,original,patched=profile
 if data[offset:offset+4]!=original:raise ValueError('Z menu branch mismatch')
 out=bytearray(data);out[offset:offset+4]=patched;return bytes(out)

if __name__=='__main__':
 if len(sys.argv)!=3:raise SystemExit('Usage: patch_zoffset44.py INPUT OUTPUT')
 src,dst=map(Path,sys.argv[1:]);out=patch(src.read_bytes())
 with dst.open('xb') as f:f.write(out)
 dst.chmod(0o775);print('EXPERIMENTAL Z PATCH',hashlib.sha256(out).hexdigest())
