#!/usr/bin/env python3
"""Offline experimental Dual Trust port, pinned to OTA 02.01.01.07."""
import hashlib,json,struct,sys
from pathlib import Path
BASE=Path(__file__).resolve().parent

def patch(src):
 m=json.loads((BASE/'dual44-manifest.json').read_text());payload=(BASE/'dual_verify_44.bin').read_bytes();d=bytearray(src)
 if hashlib.sha256(d).hexdigest()!=m['stock_sha256']:raise ValueError('Unsupported daemon: stock SHA256 mismatch')
 if hashlib.sha256(payload).hexdigest()!=m['payload_sha256'] or len(payload)!=m['payload_size']:raise ValueError('Payload mismatch')
 if d[:7]!=b'\x7fELF\x01\x01\x01' or struct.unpack_from('<H',d,18)[0]!=40:raise ValueError('Expected ARM32 ELF')
 phoff=struct.unpack_from('<I',d,28)[0];entsz,num=struct.unpack_from('<HH',d,42)
 if entsz!=32 or num!=8:raise ValueError('Unexpected ELF layout')
 note=phoff+m['note_ph_index']*entsz;rw=phoff+m['rw_ph_index']*entsz
 if struct.unpack_from('<8I',d,note)!=(4,0x150,0x10150,0x10150,0x20,0x20,4,4):raise ValueError('NOTE segment mismatch')
 rwfields=struct.unpack_from('<8I',d,rw)
 if rwfields!=(1,0x87000,0xa7000,0xa7000,0x830,m['rw_memsz_original'],6,0x10000):raise ValueError('RW segment mismatch')
 h=m['hook_file_offset']
 if d[h:h+4]!=bytes.fromhex(m['hook_original']):raise ValueError('Hook instruction mismatch')
 off=m['payload_file_offset'];va=m['payload_va']
 if off<len(d) or off%65536 or va%65536 or va<0xa7000+m['rw_memsz_patched']:raise ValueError('New segment overlaps')
 struct.pack_into('<8I',d,note,1,off,va,va,len(payload),len(payload),5,65536)
 struct.pack_into('<I',d,rw+20,m['rw_memsz_patched'])
 d[h:h+4]=bytes.fromhex(m['hook_patched']);d.extend(bytes(off-len(d)));d.extend(payload)
 return bytes(d)

if __name__=='__main__':
 if len(sys.argv)!=3:raise SystemExit('Usage: patch_dual44.py INPUT OUTPUT')
 src,dst=map(Path,sys.argv[1:])
 data=patch(src.read_bytes())
 with dst.open('xb') as f:f.write(data)
 dst.chmod(0o755)
 print('EXPERIMENTAL PATCH GENERATED',hashlib.sha256(data).hexdigest())
