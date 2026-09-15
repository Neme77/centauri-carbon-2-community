#!/usr/bin/env python3
"""Centauri Carbon 2 02.01.00.00 unified Dual-Trust firmware builder V3.8.

Builds the following chain (V3.8 full OTA install still requires validation):
 official stock .zip.sig -> verify/decode stock SWU -> Z-offset + OpenSSH + Dual Trust v2 + HTTP/upload v1 + frozen MQTT v2/webcam reference -> rebuilt SWU
 -> encrypted/signed ELEG 0x80 -> encrypted/signed manifest ELEG 0x83
 -> ZIP -> plain signed outer ELEG 0x04.

The tool is intentionally fail-closed and pinned to the known CC2 02.01.00.00
stock/component hashes recovered from the validated development pipeline.
V3.8 uses ZIP_STORED and Dual Trust v2. Historical v1 golden image hashes
are informational only: v2 intentionally changes daemon contents.
"""
import shlex
import datetime
from pathlib import Path
import argparse, hashlib, json, os, shutil, stat, struct, subprocess, sys, tempfile, zipfile

HEADER_SIZE=0x200; OFF_FLAGS=0x004; OFF_VERSION=0x005; OFF_SUBTYPE=0x006; OFF_DATA_SIZE=0x008
OFF_FILENAME=0x010; OFF_CRYPT_START=0x090; OFF_CRYPT_SPAN=0x098; OFF_IV=0x0A0
OFF_PAYLOAD_SIZE=0x0B0; OFF_SHA256=0x0E0; OFF_RSA=0x100; RSA_SIZE=256
CPIO_FILES=["sw-description","resource","uboot","boot0","kernel","rootfs","cpio_item_md5"]
MD5_FILES=["sw-description","resource","uboot","boot0","kernel","rootfs"]
EXPECTED={
 "stock_swu":"5ca67416fd59b98ecb3e2f96af518cb650d628d214b3a90a9d3d92cd6bb7f444",
 "sw-description":"6598586ada5e7e9256316c63b801579e65e797d2a77e13ab360f09b3581ae2fb",
 "resource":"aacfe9a9d847f4be3e4b5a16f7c16cba93a60e25ba371efb77568568b0b37aad",
 "uboot":"c2b29efac7d547bffabfcb583aeaca623adaa851d093266b8fb11baef47c92d5",
 "boot0":"b18b3f31b3d378b56b4d01b1863a6b9f0f61211ba1f4ad8e7b672ff54887ec41",
 "kernel":"a61fdd2413992e57022a71e0f7b18ac37ec6feada4faadb80e902cd3c9241bf3",
 "stock_rootfs":"a9b583656cf1a275ba8695dc53f1b7e2f18a7d2c15e5224acb961c6f240451e2",
 "stock_gui":"a231c26bc965b0e2ee5edbf4fc1ca4018fed009da601a7e7dc267a0c81e7fb08",
 "patched_gui":"afa2b1f181d18dc4803a60ee61fb3fc454f1e91afa12743bc1a1e5e386e439ff",
 "sshd":"7746b7085a1539f0a8db1aea89a97ea0ed7e8e5e722fecd476fdedfa1e7a002c",
 "sshd_init":"69b42d5f0adf20eeffd5218bc9e955af1d3590d68a07c21ad90128ea7a6930d8",
 "sshd_config":"0b8f561d681101fff24bb5f65f469f6ea8f838d5c12576f104b73c85392eb066",
 "daemon_stock":"b760fd80bb03de28ac348756a9a1c5311066377876570d2fdfd45fb562d91cdb",
 "dual_payload":"33d013ca4a63334fdbbd0adb77bd10f81e2e21c3ac41229e216eb17e5949d06f",
 "daemon_dual":"3af6104d0ac76cc043ecf38985e1b00a0d5ceace0a4f4b66820e21f4735a98c7",

 "stock_outer":"219fc28e9845f5d70e3a4499b60404d80368e5aa7e52eafb3e5e9c17c5e02926",
 "stock_inner":"03d0c633cbd7842a6e259f38574d0b99bb8b72036add6b7a23b22b00d9b36331",
 "aes_key":"70adfb48a2156086ad2495cfc121557f455dd5373450017d8da1dcacd7cf1009",
 "zoffset_patcher":"a233e6eb18993bf2fa82534ceca3e351ccf7069654c7f28c01b1eadb6a2bc8a4",
 "dual_patcher":"32c2e71964b9194da02182d145cf656e8b788cd9460d8b12e8a27a74d2d252cd",
 "sig_tool":"704aab8e544d3aa92b3041183c431a544c17b95f8dcd766367abf7166372bf8f",
 "mksquashfs":"6b457f6a5588aa66092aad192c09072e2de597d21d5ba990dee878353cd18844",
 "unsquashfs":"6b0d44d206a64f534e79e4a8062f2e70854fa464de0c9b83ae8eb19dfec5db23",
}
GOLDEN={
 "rootfs":"b9e46320d01d6c3fcc5e35a67c74e81df506afaba754b7a69b5502c78871015b",
 "swu":"d3c0f5287c61cde457938a7c18600e0148873618c5f10f3acff8de1a50f92a8c",
 "swusig":"b6872abd5131fbbdb8075619c56a1e5908d3d601f7698310bb1c698cfd5ff0d4",
 "zip":"564ce9c760763109dd1c876b072357000f58622fa7f04ca8ccbbeacc7be81173",
 "outer":"eeadf35d6310b7f847bc064d744c01fedf918e0b34e100cde37a3a0b2da0f2a4",
}
HOOK_OFFSET=0x2C68C; INJECT_OFFSET=0x852AC; PAYLOAD_SIZE=0x318
ELF_FILESZ_OFFSET=0xA4; ELF_MEMSZ_OFFSET=0xA8; OLD_SEGMENT_SIZE=0x852AC; NEW_SEGMENT_SIZE=0x855C4
STOCK_HOOK=bytes.fromhex("4f fb ff eb"); DUAL_HOOK=bytes.fromhex("06 63 01 eb")

def sha256(p):
 h=hashlib.sha256();
 with open(p,'rb') as f:
  for b in iter(lambda:f.read(1024*1024),b''): h.update(b)
 return h.hexdigest()
def md5(p):
 h=hashlib.md5();
 with open(p,'rb') as f:
  for b in iter(lambda:f.read(1024*1024),b''): h.update(b)
 return h.hexdigest()
def req(p,label):
 p=Path(p)
 if not p.is_file(): raise RuntimeError(f"Missing {label}: {p}")
 return p
def reqhash(p,h,label):
 a=sha256(p)
 if a!=h: raise RuntimeError(f"{label} SHA256 mismatch\n expected {h}\n actual   {a}")
 print(f"[OK] {label}: {a[:16]}...")
def run(cmd,cwd=None,stdin=None,capture=False):
 r=subprocess.run([str(x) for x in cmd],cwd=cwd,stdin=stdin,stdout=subprocess.PIPE if capture else None,stderr=subprocess.STDOUT if capture else None,text=capture)
 if r.returncode: raise RuntimeError(f"Command failed ({r.returncode}): {' '.join(map(str,cmd))}\n{r.stdout or ''}")
 return r.stdout or ''
def banner(x): print("\n"+"="*72+"\n"+x+"\n"+"="*72)

def golden_report(label,path,key):
 a=sha256(path); e=GOLDEN[key]; ok=(a==e)
 print(f"[{'GOLDEN' if ok else 'DIFF'}] {label}: {a}")
 if not ok: print(f"       reference: {e}")
 return ok

def sign_digest(key,digest):
 r=subprocess.run(["openssl","pkeyutl","-sign","-inkey",str(key),"-pkeyopt","digest:sha256","-pkeyopt","rsa_padding_mode:pkcs1"],input=digest,stdout=subprocess.PIPE,stderr=subprocess.PIPE)
 if r.returncode: raise RuntimeError(r.stderr.decode(errors='replace'))
 if len(r.stdout)!=RSA_SIZE: raise RuntimeError("Unexpected RSA signature size")
 return r.stdout

def aes_cbc(data,key,iv,decrypt=False):
 r=subprocess.run(["openssl","enc","-aes-256-cbc","-d" if decrypt else "-e","-nopad","-K",key.hex(),"-iv",iv.hex()],input=data,stdout=subprocess.PIPE,stderr=subprocess.PIPE)
 if r.returncode: raise RuntimeError(r.stderr.decode(errors='replace'))
 return r.stdout

def eleg_encrypted(plain,out,aes_key,priv,filename,eleg_type):
 if eleg_type not in (0x80,0x83): raise RuntimeError("Encrypted ELEG must be 0x80 or 0x83")
 pad=16-(len(plain)%16); padded=plain+bytes([pad])*pad; iv=os.urandom(16); cipher=aes_cbc(padded,aes_key,iv)
 d=hashlib.sha256(cipher).digest(); sig=sign_digest(priv,d); h=bytearray(HEADER_SIZE); name=filename.encode()
 h[:4]=b'ELEG'; h[OFF_FLAGS]=eleg_type; h[OFF_VERSION]=1; struct.pack_into('<H',h,OFF_SUBTYPE,2); struct.pack_into('<Q',h,OFF_DATA_SIZE,len(plain))
 h[OFF_FILENAME:OFF_FILENAME+len(name)]=name; struct.pack_into('<Q',h,OFF_CRYPT_START,0); struct.pack_into('<Q',h,OFF_CRYPT_SPAN,len(cipher)); h[OFF_IV:OFF_IV+16]=iv; struct.pack_into('<Q',h,OFF_PAYLOAD_SIZE,len(cipher)); h[OFF_SHA256:OFF_SHA256+32]=d; h[OFF_RSA:OFF_RSA+256]=sig
 Path(out).write_bytes(h+cipher)

def eleg_plain(payload,out,priv,filename):
 d=hashlib.sha256(payload).digest(); sig=sign_digest(priv,d); h=bytearray(HEADER_SIZE); name=filename.encode()
 h[:4]=b'ELEG'; h[OFF_FLAGS]=0x04; h[OFF_VERSION]=1; struct.pack_into('<H',h,OFF_SUBTYPE,2); struct.pack_into('<Q',h,OFF_DATA_SIZE,len(payload)); h[OFF_FILENAME:OFF_FILENAME+len(name)]=name; struct.pack_into('<Q',h,OFF_PAYLOAD_SIZE,len(payload)); h[OFF_SHA256:OFF_SHA256+32]=d; h[OFF_RSA:OFF_RSA+256]=sig; Path(out).write_bytes(h+payload)

def check_eleg(path,want):
 b=Path(path).read_bytes();
 if len(b)<HEADER_SIZE or b[:4]!=b'ELEG': raise RuntimeError(f"Invalid ELEG: {path}")
 typ=b[OFF_FLAGS]; psz=struct.unpack_from('<Q',b,OFF_PAYLOAD_SIZE)[0]; payload=b[HEADER_SIZE:]
 if typ!=want: raise RuntimeError(f"Wrong ELEG type {typ:#x}, expected {want:#x}")
 if psz!=len(payload): raise RuntimeError("ELEG payload size mismatch")
 if hashlib.sha256(payload).digest()!=b[OFF_SHA256:OFF_SHA256+32]: raise RuntimeError("ELEG payload SHA mismatch")
 print(f"[OK] {Path(path).name}: ELEG {want:#04x}")

def eleg_info(path):
 b=Path(path).read_bytes()
 if len(b)<HEADER_SIZE or b[:4]!=b'ELEG': raise RuntimeError(f"Invalid ELEG: {path}")
 def u64(o): return struct.unpack_from('<Q',b,o)[0]
 name=b[OFF_FILENAME:OFF_CRYPT_START].split(b'\0',1)[0].decode('utf-8',errors='replace')
 return {"type":b[OFF_FLAGS],"version":b[OFF_VERSION],"subtype":struct.unpack_from('<H',b,OFF_SUBTYPE)[0],
         "data_size":u64(OFF_DATA_SIZE),"filename":name,"crypt_start":u64(OFF_CRYPT_START),
         "crypt_span":u64(OFF_CRYPT_SPAN),"iv":b[OFF_IV:OFF_IV+16],"payload_size":u64(OFF_PAYLOAD_SIZE),
         "sha":b[OFF_SHA256:OFF_SHA256+32],"payload":b[HEADER_SIZE:]}

def validate_eleg(path,want,encrypted=None):
 x=eleg_info(path); payload=x['payload']
 if x['type']!=want or x['version']!=1 or x['subtype']!=2: raise RuntimeError(f"Unexpected ELEG identity in {path}: type={x['type']:#x} version={x['version']} subtype={x['subtype']}")
 if x['payload_size']!=len(payload): raise RuntimeError(f"ELEG payload size mismatch: {path}")
 if hashlib.sha256(payload).digest()!=x['sha']: raise RuntimeError(f"ELEG payload SHA mismatch: {path}")
 if encrypted is True:
  if x['crypt_start']!=0 or x['crypt_span']!=len(payload) or len(payload)%16: raise RuntimeError(f"Invalid encrypted ELEG geometry: {path}")
 if encrypted is False:
  if x['crypt_start']!=0 or x['crypt_span']!=0 or x['iv']!=bytes(16): raise RuntimeError(f"Invalid plain ELEG geometry: {path}")
 return x

def pkcs7_unpad(data):
 if not data or len(data)%16: raise RuntimeError('Invalid AES padded length')
 n=data[-1]
 if n<1 or n>16 or data[-n:]!=bytes([n])*n: raise RuntimeError('Invalid PKCS#7 padding')
 return data[:-n]

def decode_stock_package(stock_pkg,aes_key,work):
 banner('VERIFY + DECODE OFFICIAL STOCK PACKAGE')
 reqhash(stock_pkg,EXPECTED['stock_outer'],'official stock outer .zip.sig')
 x=validate_eleg(stock_pkg,0x04,encrypted=False)
 verify_signature(stock_pkg,STOCK_PUBLIC)
 if '02.01.00.00' not in x['filename']: raise RuntimeError(f"Unexpected stock embedded filename: {x['filename']}")
 outer_zip=work/'stock_outer.zip'; outer_zip.write_bytes(x['payload'])
 if not zipfile.is_zipfile(outer_zip): raise RuntimeError('Stock outer payload is not a ZIP')
 with zipfile.ZipFile(outer_zip,'r') as z:
  members=[n for n in z.namelist() if n.lower().endswith('.swu.sig') and not n.endswith('/')]
  if len(members)!=1: raise RuntimeError(f"Expected one stock .swu.sig, found: {members}")
  n=members[0]; pp=Path(n)
  if pp.is_absolute() or '..' in pp.parts: raise RuntimeError(f"Unsafe ZIP member: {n}")
  inner=work/'stock_inner.swu.sig'; inner.write_bytes(z.read(n))
  manifest=work/'stock_manifest.sig'; manifest.write_bytes(z.read('ota-package-list.json.sig'))
  mx=validate_eleg(manifest,0x83,encrypted=True); verify_signature(manifest,STOCK_PUBLIC)
  mp=pkcs7_unpad(aes_cbc(mx['payload'],aes_key,mx['iv'],decrypt=True))
  if len(mp)!=mx['data_size']: raise RuntimeError('Stock manifest length mismatch')
  packages=json.loads(mp)['packages']
  if not any(v.get('file')==n and v.get('hash')==sha256(inner) for v in packages): raise RuntimeError('Stock manifest/SWU link mismatch')
 reqhash(inner,EXPECTED['stock_inner'],'official stock inner SWU.SIG')
 y=validate_eleg(inner,0x80,encrypted=True)
 verify_signature(inner,STOCK_PUBLIC)
 dec=aes_cbc(y['payload'],aes_key,y['iv'],decrypt=True)
 plain=pkcs7_unpad(dec)
 if len(plain)!=y['data_size']: raise RuntimeError('Decoded stock SWU size mismatch')
 swu=work/'stock_decrypted.swu'; swu.write_bytes(plain)
 reqhash(swu,EXPECTED['stock_swu'],'decrypted stock SWU')
 if plain[:6] not in (b'070701',b'070702'): raise RuntimeError('Decoded stock SWU is not newc/crc CPIO')
 print(f"[OK] Stock chain: outer 0x04 -> ZIP -> SWU.SIG 0x80 -> SWU")
 return swu

def find_squash_tool(explicit,name):
 if explicit:
  return req(explicit,name)
 here=Path(__file__).resolve().parent
 candidates=[here/'tools'/'squashfs-tools-4.6.1'/'bin'/name, here/'squashfs-tools-4.6.1'/'bin'/name]
 for c in candidates:
  if c.is_file(): return c
 q=shutil.which(name)
 if q: return Path(q)
 raise RuntimeError(f"Cannot find {name}; pass --{name.replace('squashfs','squashfs')}")

def version_output(cmd):
 # squashfs-tools 4.6.1 may print a valid version banner but return 1.
 r=subprocess.run([str(cmd),'-version'],stdout=subprocess.PIPE,stderr=subprocess.STDOUT,text=True)
 return r.returncode, r.stdout or ''

def verify_squashfs(mk,un,work,manifest=None):
 banner('VERIFY SQUASHFS TOOLCHAIN')
 hashes=EXPECTED
 if manifest:
  record=json.loads(Path(manifest).read_text())
  if record.get('source_commit')!='d8cb82d9840330f9344ec37b992595b5d7b44184': raise RuntimeError('Unexpected toolchain source commit')
  hashes=record['sha256']
 for name,path in [('mksquashfs',mk),('unsquashfs',un)]: reqhash(path,hashes[name],name+' 4.6.1')
 mrc,mv=version_output(mk); urc,uv=version_output(un)
 if '4.6.1' not in mv or '4.6.1' not in uv: raise RuntimeError('SquashFS Tools 4.6.1 required')
 print(f'[OK] mksquashfs -version (rc={mrc}) contains 4.6.1')
 print(f'[OK] unsquashfs -version (rc={urc}) contains 4.6.1')
 t=work/'sqfs_test'; src=t/'src'; src.mkdir(parents=True); (src/'test.txt').write_bytes(b'CC2 self-test\n')
 img=t/'t.sqfs'; out=t/'out'
 log=run([mk,src,img,'-comp','xz','-b','262144','-noappend','-no-tailends','-exports','-all-root'],capture=True)
 if 'Failed to read file' in log: raise RuntimeError('SquashFS sentinel failed: Failed to read file')
 run([un,'-d',out,img],capture=True)
 if (out/'test.txt').read_bytes()!=b'CC2 self-test\n': raise RuntimeError('SquashFS round-trip mismatch')
 print('[OK] SquashFS Tools 4.6.1 round-trip')

def patch_zoffset(gui):
 reqhash(gui,EXPECTED['stock_gui'],'stock GUI'); data=bytearray(Path(gui).read_bytes()); off=0x63C5C
 if data[off:off+4]!=bytes.fromhex('63 e7 00 eb'): raise RuntimeError('Z-offset original bytes mismatch')
 data[off:off+4]=bytes.fromhex('00 01 00 ea'); Path(gui).write_bytes(data); os.chmod(gui,0o775); reqhash(gui,EXPECTED['patched_gui'],'patched GUI')

def patch_dual(daemon,payload):
 reqhash(daemon,EXPECTED['daemon_stock'],'stock update daemon'); reqhash(payload,EXPECTED['dual_payload'],'Dual Trust payload')
 data=bytearray(Path(daemon).read_bytes()); pl=Path(payload).read_bytes()
 if len(pl)!=PAYLOAD_SIZE or data[HOOK_OFFSET:HOOK_OFFSET+4]!=STOCK_HOOK or any(data[INJECT_OFFSET:INJECT_OFFSET+PAYLOAD_SIZE]): raise RuntimeError('Dual Trust precondition failed')
 fs=int.from_bytes(data[ELF_FILESZ_OFFSET:ELF_FILESZ_OFFSET+4],'little'); ms=int.from_bytes(data[ELF_MEMSZ_OFFSET:ELF_MEMSZ_OFFSET+4],'little')
 if (fs,ms)!=(OLD_SEGMENT_SIZE,OLD_SEGMENT_SIZE): raise RuntimeError('Unexpected daemon ELF segment size')
 if int.from_bytes(data[0xC8:0xCC],'little')!=0x1EB0: raise RuntimeError('Unexpected RW segment size')
 data[0xC8:0xCC]=(0x1EF4).to_bytes(4,'little')
 data[ELF_FILESZ_OFFSET:ELF_FILESZ_OFFSET+4]=NEW_SEGMENT_SIZE.to_bytes(4,'little'); data[ELF_MEMSZ_OFFSET:ELF_MEMSZ_OFFSET+4]=NEW_SEGMENT_SIZE.to_bytes(4,'little'); data[INJECT_OFFSET:INJECT_OFFSET+PAYLOAD_SIZE]=pl; data[HOOK_OFFSET:HOOK_OFFSET+4]=DUAL_HOOK; Path(daemon).write_bytes(data)
 reqhash(daemon,EXPECTED['daemon_dual'],'Dual Trust daemon')


BASE=Path(__file__).resolve().parent
STOCK_PUBLIC=BASE/'keys/cc2_stock_public.pem'
COMMUNITY_PUBLIC=BASE/'dualtrust/cc2_community_release_public.pem'
PUBLIC_HASHES={'stock': '39f285d0fac0a14d5cec7e07e5c3a832e31f208de83febb7152989053fc07f69', 'community': 'b960758986d9daa89d4d61ff96695c0fb6dcaed93025f09f32c595bb05b32e37'}

def public_der(path,private=False):
 cmd=['openssl','pkey','-in',str(path),'-pubout','-outform','DER']
 if not private: cmd.append('-pubin')
 r=subprocess.run(cmd,stdout=subprocess.PIPE,stderr=subprocess.PIPE)
 if r.returncode: raise RuntimeError('Cannot read signing/public key')
 return r.stdout

def signing_public(mode):
 if mode not in ('stock','community'): raise RuntimeError('Unknown signing mode')
 key=STOCK_PUBLIC if mode=='stock' else COMMUNITY_PUBLIC
 reqhash(key,PUBLIC_HASHES[mode],mode+' public key')
 return key

def verify_digest(public,digest,signature):
 if len(digest)!=32 or len(signature)!=256: raise RuntimeError('Invalid RSA-2048/SHA256 geometry')
 with tempfile.TemporaryDirectory(prefix='cc2_verify_') as d:
  sig=Path(d)/'signature'; sig.write_bytes(signature)
  r=subprocess.run(['openssl','pkeyutl','-verify','-pubin','-inkey',str(public),'-sigfile',str(sig),'-pkeyopt','digest:sha256','-pkeyopt','rsa_padding_mode:pkcs1'],input=digest,stdout=subprocess.PIPE,stderr=subprocess.PIPE)
  if r.returncode: raise RuntimeError('RSA signature verification failed')

def verify_signing_key(private,mode='community'):
 public=signing_public(mode)
 if public_der(private,True)!=public_der(public): raise RuntimeError('Signing key does not match selected '+mode+' public key')
 # Keep the embedded fallback community key fixed in BOTH signing modes.
 community=signing_public('community')
 reqhash(BASE/'dualtrust/dual_verify.bin',EXPECTED['dual_payload'],'Dual Trust v2 payload')
 if community.read_bytes() not in (BASE/'dualtrust/dual_verify.bin').read_bytes(): raise RuntimeError('Community PEM missing from payload')
 challenge=hashlib.sha256(b'CC2 builder v3.8 local signing-key check'+os.urandom(32)).digest()
 verify_digest(public,challenge,sign_digest(private,challenge))
 print('[OK] '+mode+' RSA-2048 key match and signing/verification self-test')
 return public

def verify_signature(path,public):
 if Path(public)==STOCK_PUBLIC: mode='stock'
 elif Path(public)==COMMUNITY_PUBLIC: mode='community'
 else: raise RuntimeError('Unknown verification public key')
 signing_public(mode)
 b=Path(path).read_bytes()
 if len(b)<512 or b[:4]!=b'ELEG' or hashlib.sha256(b[512:]).digest()!=b[224:256]: raise RuntimeError('ELEG digest mismatch')
 verify_digest(public,b[224:256],b[256:512])
 print('[OK] '+mode+' RSA signature: '+Path(path).name)


PRINTER_STOCK_SHA256="c968ad1430f331bf0a20d5e54bdc8c1428a223b5977c3320c1287e301ec447e5"
PRINTER_PATCHED_SHA256="fb95bd123fbbb9e77ed380a0ab7bdb53a0b86997af8452adf76710a2d242e021"
PRINTER_PATCHED_MD5="b3a607b6d1db7b3d4224c2b3b5f0341a"
HTTP_UPLOAD_MANIFEST=Path(__file__).resolve().parent/'patches/http-upload/http_upload_v1.json'
HTTP_UPLOAD_MANIFEST_SHA256="3f41a27fa289fd4f900e374e77b980b970740f066374fcaa9a4624870bc35573"

def load_http_upload_manifest(path=None):
 path=HTTP_UPLOAD_MANIFEST if path is None else Path(path)
 reqhash(path,HTTP_UPLOAD_MANIFEST_SHA256,'HTTP/upload v1 patch manifest')
 m=json.loads(path.read_text())
 if m['baseline']['sha256']!=PRINTER_STOCK_SHA256 or m['variant']['sha256']!=PRINTER_PATCHED_SHA256:
  raise RuntimeError('Wrong HTTP/upload reference')
 return m

def patch_http_upload_bytes(original,manifest=None):
 m=load_http_upload_manifest() if manifest is None else load_http_upload_manifest(manifest)
 if hashlib.sha256(original).hexdigest()!=PRINTER_STOCK_SHA256:
  raise RuntimeError('HTTP/upload requires the exact stock elegoo_printer')
 data=bytearray(original)
 for patch in m['variant']['patches']:
  off=patch['offset'];before=bytes.fromhex(patch['before']);after=bytes.fromhex(patch['after'])
  if len(before)!=len(after) or bytes(data[off:off+len(before)])!=before:
   raise RuntimeError('HTTP/upload original bytes mismatch')
  data[off:off+len(after)]=after
 if hashlib.sha256(data).hexdigest()!=PRINTER_PATCHED_SHA256 or hashlib.md5(data).hexdigest()!=PRINTER_PATCHED_MD5:
  raise RuntimeError('HTTP/upload result differs from the user-tested combined v1')
 return bytes(data)

def patch_http_upload(printer):
 # All checks complete in memory before writing the extracted working copy.
 printer=Path(printer);patched=patch_http_upload_bytes(printer.read_bytes())
 printer.write_bytes(patched)
 reqhash(printer,PRINTER_PATCHED_SHA256,'HTTP/upload combined v1 installed')


# V3.8 installs the exact field-tested binary as a pinned external component.
# MQTT v1 sources are not available in this source snapshot. Do not claim a
# source-only reconstruction of that step. Firmware/private keys stay external.
PRINTER_RELEASE_SHA256="c21126e78a63ac9140e7347c56f363d5e513495c58c06fc17e8ef97846f3c0a3"
PRINTER_RELEASE_MD5="06071f7b3ca6f809d5a329e1a9466f4c"
PRINTER_RELEASE_SIZE=18099452
PRINTER_RELEASE_DEFAULT=Path(__file__).resolve().parent/'components/printer/elegoo_printer_v3_8'

def validate_release_printer(data):
 if len(data)!=PRINTER_RELEASE_SIZE or hashlib.sha256(data).hexdigest()!=PRINTER_RELEASE_SHA256 or hashlib.md5(data).hexdigest()!=PRINTER_RELEASE_MD5:
  raise RuntimeError('V3.8 requires the exact field-tested MQTT v2 + webcam binary (MD5 06071f7b...)')
 # Ten HTTP/upload v1 regions remain byte-identical. The network startup block
 # at 0x5b9124 was intentionally extended by MQTT coexistence; the whole-file
 # hash above pins that block as well. No optional unchecked patch is accepted.
 m=load_http_upload_manifest()
 for patch in m['variant']['patches']:
  if patch['offset']==0x5a9124: continue
  off=patch['offset']; expected=bytes.fromhex(patch['after'])
  if data[off:off+len(expected)]!=expected:
   raise RuntimeError('V3.8 HTTP/upload guard regression')
 if data[0x5b3ca8:0x5b3cac]!=bytes.fromhex('97d7feeb'):
  raise RuntimeError('V3.8 WAN webcam branch mismatch')
 return data

def install_release_printer(printer,reference):
 # This operates only on the builder's extracted rootfs, never on a live printer.
 printer=Path(printer)
 reqhash(printer,PRINTER_PATCHED_SHA256,'V3.7 HTTP/upload intermediate')
 data=validate_release_printer(Path(reference).read_bytes())
 printer.write_bytes(data)
 reqhash(printer,PRINTER_RELEASE_SHA256,'V3.8 MQTT/webcam installed')

def main():
 ap=argparse.ArgumentParser(description='CC2 02.01.00.00 Dual-Trust firmware builder V3.8 self-contained + functional golden regression checks')
 here=Path(__file__).resolve().parent
 ap.add_argument('stock_package',nargs='?',default=str(here/'original_firmware'/'cc2_eeb001_02.01.00.00_20260707170825.zip.sig'),help='official stock .zip.sig (default: original_firmware/...)')
 ap.add_argument('--signing-mode',choices=['stock','community'],default='community',help='stock: initial install; community: later updates')
 ap.add_argument('--private-key',help='Private key for selected mode; explicit path required in stock mode')
 ap.add_argument('--check-signing-key-only',action='store_true',help='Check key pair and sign/verify a local challenge; no firmware or AES required')
 ap.add_argument('--aes-key',default=str(here/'keys'/'cc2_aes_key_v1.bin'))
 ap.add_argument('--toolchain-manifest',help='Explicit local build manifest; hashes checked in addition to version and round-trip')
 ap.add_argument('--mksquashfs'); ap.add_argument('--unsquashfs')
 ap.add_argument('--printer-reference',default=str(PRINTER_RELEASE_DEFAULT),help='Exact field-tested V3.8 printer component; hash-pinned')
 ap.add_argument('--sshd',default=str(here/'components'/'ssh'/'sshd'))
 ap.add_argument('--sshd-init',default=str(here/'components'/'ssh'/'sshd.init'))
 ap.add_argument('--sshd-config',default=str(here/'components'/'ssh'/'sshd_config'))
 ap.add_argument('--dual-payload',default=str(here/'dualtrust'/'dual_verify.bin'))
 ap.add_argument('--zoffset-patcher',default=str(here/'patches'/'z-offset'/'patch_zoffset.py'))
 ap.add_argument('--dual-patcher',default=str(here/'dualtrust'/'apply_dualtrust.py'))
 ap.add_argument('--sig-tool',default=str(here/'tools'/'cc2_sig_tool_v1.1.py'))
 ap.add_argument('--output',help='New output path; default name reflects signing mode')
 ap.add_argument('--keep-work',action='store_true')
 ap.add_argument('--preflight-only',action='store_true',help='verify/decode stock and prerequisites, then stop')
 ap.add_argument('--golden-check',action='store_true',help='compare generated stages with the validated historical golden hashes')
 ap.add_argument('--require-golden',action='store_true',help='fail on functional/content regression; byte-level metadata differences are informational')
 a=ap.parse_args()
 if a.signing_mode=='stock' and not a.private_key: ap.error('--signing-mode stock requires --private-key')
 priv=req(a.private_key or here/'keys'/'cc2_community_release_private.pem','private key')
 signing_key=verify_signing_key(priv,a.signing_mode)
 if a.check_signing_key_only:
  print('SIGNING KEY CHECK PASS; no firmware built or installed'); return 0
 release_tag='STOCK_BOOTSTRAP' if a.signing_mode=='stock' else 'COMMUNITY'
 a.output=a.output or ('CC2_FULL_V3_8_'+release_tag+'.zip.sig')
 stock_pkg=req(a.stock_package,'official stock .zip.sig')
 aes_path=req(a.aes_key,'AES key'); reqhash(aes_path,EXPECTED['aes_key'],'AES key v1'); aes=aes_path.read_bytes()
 if len(aes)!=32: raise RuntimeError('AES key must be 32 bytes')
 sshd=req(a.sshd,'sshd'); init=req(a.sshd_init,'sshd.init'); conf=req(a.sshd_config,'sshd_config'); dual=req(a.dual_payload,'dual_verify.bin')
 reqhash(sshd,EXPECTED['sshd'],'sshd'); reqhash(init,EXPECTED['sshd_init'],'sshd.init'); reqhash(conf,EXPECTED['sshd_config'],'sshd_config'); reqhash(dual,EXPECTED['dual_payload'],'Dual Trust payload')
 zpatch=req(a.zoffset_patcher,'Z-offset patcher'); dpatch=req(a.dual_patcher,'Dual Trust patcher'); sigtool=req(a.sig_tool,'cc2_sig_tool_v1.1.py')
 reqhash(zpatch,EXPECTED['zoffset_patcher'],'Z-offset patcher'); reqhash(dpatch,EXPECTED['dual_patcher'],'Dual Trust patcher'); reqhash(sigtool,EXPECTED['sig_tool'],'cc2_sig_tool v1.1')
 load_http_upload_manifest()
 printer_reference=req(a.printer_reference,'V3.8 printer reference')
 validate_release_printer(printer_reference.read_bytes())
 mk=find_squash_tool(a.mksquashfs,'mksquashfs'); un=find_squash_tool(a.unsquashfs,'unsquashfs')
 for cmd in ('openssl','cpio'):
  if not shutil.which(cmd): raise RuntimeError(f'Missing required command: {cmd}')
 work=Path(tempfile.mkdtemp(prefix='cc2_dualtrust_')); cpio=work/'cpio'; root=work/'rootfs'; cpio.mkdir(); target=Path(a.output).resolve(); out=work/'validated-output.zip.sig'
 if target.exists() or target.is_symlink(): raise RuntimeError('Output already exists; choose a new path')
 target.parent.mkdir(parents=True,exist_ok=True)
 try:
  verify_squashfs(mk,un,work,a.toolchain_manifest)
  stock=decode_stock_package(stock_pkg,aes,work)
  if a.preflight_only:
   banner('PREFLIGHT PASS'); print('Stock package and prerequisites verified.'); print('Work:',work if a.keep_work else '(removed)'); return 0
  banner('EXTRACT STOCK SWU')
  with stock.open('rb') as f: run(['cpio','-idmu'],cwd=cpio,stdin=f)
  for n in CPIO_FILES: req(cpio/n,n)
  for n in ['sw-description','resource','uboot','boot0','kernel']: reqhash(cpio/n,EXPECTED[n],f'stock {n}')
  reqhash(cpio/'rootfs',EXPECTED['stock_rootfs'],'stock rootfs')
  banner('EXTRACT + PATCH ROOTFS'); run([un,'-d',root,cpio/'rootfs'],capture=True)
  gui=root/'opt/bin/ec-eeb001-gui'
  printer=root/'opt/bin/elegoo_printer'
  daemon=root/'opt/inst/daemon-000/daemon-000'
  reqhash(gui,EXPECTED['stock_gui'],'stock GUI')
  reqhash(printer,'c968ad1430f331bf0a20d5e54bdc8c1428a223b5977c3320c1287e301ec447e5','stock elegoo_printer')
  reqhash(daemon,EXPECTED['daemon_stock'],'stock update daemon')
  patch_zoffset(gui)
  patch_http_upload(printer)
  install_release_printer(printer,printer_reference)
  (root/'usr/sbin').mkdir(parents=True,exist_ok=True); shutil.copy2(sshd,root/'usr/sbin/sshd'); os.chmod(root/'usr/sbin/sshd',0o755)
  (root/'etc/init.d').mkdir(parents=True,exist_ok=True); shutil.copy2(init,root/'etc/init.d/sshd'); os.chmod(root/'etc/init.d/sshd',0o755)
  (root/'etc/ssh').mkdir(parents=True,exist_ok=True); shutil.copy2(conf,root/'etc/ssh/sshd_config'); os.chmod(root/'etc/ssh/sshd_config',0o644)
  rc=root/'etc/rc.d'; rc.mkdir(parents=True,exist_ok=True)
  for n in ('S50sshd','K50sshd'):
   p=rc/n
   if p.exists() or p.is_symlink(): p.unlink()
   p.symlink_to('../init.d/sshd')
  for n in ('ssh_host_rsa_key','ssh_host_rsa_key.pub','ssh_host_ecdsa_key','ssh_host_ecdsa_key.pub','ssh_host_ed25519_key','ssh_host_ed25519_key.pub'):
   p=root/'etc/ssh'/n
   if p.exists() or p.is_symlink(): p.unlink()
  patch_dual(daemon,dual)
  # Verify intended rootfs surface before compression.
  reqhash(gui,EXPECTED['patched_gui'],'installed patched GUI'); reqhash(root/'usr/sbin/sshd',EXPECTED['sshd'],'installed sshd')
  reqhash(printer,PRINTER_RELEASE_SHA256,'installed V3.8 elegoo_printer')
  reqhash(daemon,EXPECTED['daemon_dual'],'installed Dual Trust daemon')
  banner('REBUILD SQUASHFS'); new=work/'rootfs.new'; log=run([mk,root,new,'-comp','xz','-b','262144','-noappend','-no-tailends','-exports','-all-root'],capture=True)
  if 'Failed to read file' in log: raise RuntimeError('mksquashfs reported Failed to read file')
  shutil.copyfile(new,cpio/'rootfs'); root_hash=sha256(cpio/'rootfs'); print('rootfs SHA256:',root_hash)
  g_root=golden_report('rebuilt rootfs image (byte-level; metadata may differ)',cpio/'rootfs','rootfs') if (a.golden_check or a.require_golden) else None
  # Re-extract and audit the critical files after SquashFS creation.
  vr=work/'verify_rootfs'; run([un,'-d',vr,cpio/'rootfs'],capture=True)
  reqhash(vr/'opt/bin/ec-eeb001-gui',EXPECTED['patched_gui'],'rebuilt patched GUI'); reqhash(vr/'usr/sbin/sshd',EXPECTED['sshd'],'rebuilt sshd')
  reqhash(vr/'opt/bin/elegoo_printer',PRINTER_RELEASE_SHA256,'rebuilt V3.8 elegoo_printer')
  reqhash(vr/'opt/inst/daemon-000/daemon-000',EXPECTED['daemon_dual'],'rebuilt Dual Trust daemon')
  reqhash(vr/'etc/init.d/sshd',EXPECTED['sshd_init'],'rebuilt sshd init')
  reqhash(vr/'etc/ssh/sshd_config',EXPECTED['sshd_config'],'rebuilt sshd config')
  for n in ('S50sshd','K50sshd'):
   p=vr/'etc/rc.d'/n
   if not p.is_symlink() or os.readlink(p)!='../init.d/sshd': raise RuntimeError(f'Invalid rebuilt SSH symlink: {p}')
  banner('REBUILD SWU'); data=''.join(f'{md5(cpio/n)}  {n}\n' for n in MD5_FILES).encode('ascii')
  if len(data)!=254: raise RuntimeError('cpio_item_md5 format mismatch')
  (cpio/'cpio_item_md5').write_bytes(data); swu=work/'CC2_02.01.00.00_SSH_ZOFFSET_DT2_HTTP_UPLOAD1_MQTT2_WEBCAM1.swu'
  # Match the historical WSL1-safe build path: real printf | cpio pipeline.
  # Restore the metadata used by the validated historical 02.01.00.00 build.
  for n in CPIO_FILES:
   os.chown(cpio/n,1004,1002)
  modes={'sw-description':0o775,'resource':0o664,'uboot':0o664,'boot0':0o664,'kernel':0o644,'rootfs':0o664,'cpio_item_md5':0o664}
  for n,m in modes.items(): os.chmod(cpio/n,m)
  hist_ts=datetime.datetime(2026,7,7,11,8,2).timestamp()
  for n in CPIO_FILES: os.utime(cpio/n,(hist_ts,hist_ts))
  file_list='\\n'.join(CPIO_FILES)+'\\n'
  cmd="printf '%s\\n' " + ' '.join(shlex.quote(n) for n in CPIO_FILES) + " | cpio -o -H crc > " + shlex.quote(str(swu))
  r=subprocess.run(['bash','-lc',cmd],cwd=cpio)
  if r.returncode: raise RuntimeError('cpio build failed')
  swu_hash=sha256(swu); print('SWU SHA256:',swu_hash)
  g_swu=golden_report('rebuilt SWU',swu,'swu') if (a.golden_check or a.require_golden) else None
  banner('SIGN/PACK OTA CHAIN')
  swusig_name='CC2_02.01.00.00_SSH_ZOFFSET_DT2_HTTP_UPLOAD1_MQTT2_WEBCAM1_'+release_tag+'.swu.sig'; swusig=work/swusig_name
  eleg_encrypted(swu.read_bytes(),swusig,aes,priv,swusig_name[:-4],0x80); check_eleg(swusig,0x80)
  swusig_hash=sha256(swusig)
  g_swusig=golden_report('SWU.SIG',swusig,'swusig') if (a.golden_check or a.require_golden) else None
  manifest_obj={"packages":[{"file":swusig_name,"hash":swusig_hash}],"version":"02.01.00.00","update_class":"00.00.00.00"}
  manifest=json.dumps(manifest_obj,separators=(',',':')).encode('utf-8'); mansig=work/'ota-package-list.json.sig'
  eleg_encrypted(manifest,mansig,aes,priv,'ota-package-list.json',0x83); check_eleg(mansig,0x83)
  z=work/('CC2_FULL_V3_8_'+release_tag+'.zip')
  with zipfile.ZipFile(z,'w',compression=zipfile.ZIP_STORED) as zz:
   zz.write(mansig,'ota-package-list.json.sig'); zz.write(swusig,swusig_name)
  with zipfile.ZipFile(z,'r') as zz:
   bad=[i.filename for i in zz.infolist() if i.compress_type != zipfile.ZIP_STORED]
   if bad: raise RuntimeError(f'OTA ZIP contains compressed members: {bad}')
  print('[OK] OTA ZIP members: STORED (matches validated installed package)')
  if a.golden_check or a.require_golden: g_zip=golden_report('OTA ZIP',z,'zip')
  eleg_plain(z.read_bytes(),out,priv,z.name); check_eleg(out,0x04)
  if a.golden_check or a.require_golden: g_outer=golden_report('final outer',out,'outer')
  if a.require_golden:
   # V3.8: byte hashes of rebuilt SquashFS/SWU/ELEG are not deterministic because
   # SquashFS timestamps and encrypted ELEG IVs legitimately change per build.
   # Functional regressions remain fail-closed through the stock hashes, patched
   # file hashes, user-tested HTTP/upload v1 elegoo_printer hash, SSH symlink audit and ELEG validation.
   print('[OK] Functional golden requirements passed; byte-level differences above are informational')
  # Final self-inspection of all three mandatory ELEG roles.
  validate_eleg(out,0x04,encrypted=False); validate_eleg(swusig,0x80,encrypted=True); validate_eleg(mansig,0x83,encrypted=True)
  verify_signature(out,signing_key); verify_signature(swusig,signing_key); verify_signature(mansig,signing_key)
  # Stage on the destination filesystem; hard-link publishes atomically without overwrite.
  fd,stage=tempfile.mkstemp(prefix='.cc2-publish-',dir=target.parent)
  try:
   with os.fdopen(fd,'wb') as dst, out.open('rb') as src:
    shutil.copyfileobj(src,dst); dst.flush(); os.fsync(dst.fileno())
   os.link(stage,target)
  finally:
   os.unlink(stage)
  out=target
  banner('BUILD PASS')
  print('Signing mode    :',a.signing_mode)
  print('elegoo_printer MD5:',PRINTER_RELEASE_MD5)
  print('Output          :',out); print('SHA256          :',sha256(out)); print('RootFS SHA256   :',root_hash); print('SWU SHA256      :',swu_hash); print('SWU.SIG SHA256  :',swusig_hash); print('Manifest SHA256 :',sha256(mansig)); print('ELEG chain      : 0x04 -> ZIP -> manifest 0x83 + SWU 0x80'); print('Work            :',work if a.keep_work else '(removed)')
  return 0
 finally:
  if not a.keep_work: shutil.rmtree(work,ignore_errors=True)
if __name__=='__main__':
 try: sys.exit(main())
 except Exception as e: print('BUILD FAIL:',e,file=sys.stderr); sys.exit(1)
