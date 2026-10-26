#!/usr/bin/env python3
"""Port the validated display Z menu patch to OTA 02.01.01.07, offline only."""
from pathlib import Path
import hashlib,sys
STOCK='24d0e561287a4b5a87c8ec698c98f60acf2c70511114b36224b4eb9249283abd'
OFFSET=0x652d0
ORIGINAL=bytes.fromhex('a8e400eb')
PATCHED=bytes.fromhex('000100ea')

def patch(data):
 if hashlib.sha256(data).hexdigest()!=STOCK:raise ValueError('Unsupported GUI: SHA256 mismatch')
 if data[OFFSET:OFFSET+4]!=ORIGINAL:raise ValueError('Z menu branch mismatch')
 out=bytearray(data);out[OFFSET:OFFSET+4]=PATCHED;return bytes(out)

if __name__=='__main__':
 if len(sys.argv)!=3:raise SystemExit('Usage: patch_zoffset44.py INPUT OUTPUT')
 src,dst=map(Path,sys.argv[1:]);out=patch(src.read_bytes())
 with dst.open('xb') as f:f.write(out)
 dst.chmod(0o775);print('EXPERIMENTAL Z PATCH',hashlib.sha256(out).hexdigest())
