#!/usr/bin/env python3
"""Centauri Carbon 2 02.01.01.07 unified Dual-Trust firmware builder.

Builds the following chain (full OTA install still requires validation):
 official stock .zip.sig -> verify/decode stock SWU -> Z-offset + OpenSSH + Dual Trust v2 + HTTP/upload v1 + relocated MQTT v2/webcam guards + CC2 Control -> rebuilt SWU
 -> encrypted/signed ELEG 0x80 -> encrypted/signed manifest ELEG 0x83
 -> ZIP -> plain signed outer ELEG 0x04.

The tool is intentionally fail-closed and pinned to the known CC2 02.01.01.07
stock/component hashes recovered from the verified official OTA.
This is an experimental hardware-test candidate, not a validated release.
This builder uses ZIP_STORED and the relocated Dual Trust wrapper. Historical image hashes
are informational only: v2 intentionally changes daemon contents.
"""
import shlex
import datetime
from pathlib import Path
import importlib.util
import argparse, hashlib, json, os, re, shutil, stat, struct, subprocess, sys, tempfile, zipfile

HEADER_SIZE=0x200; OFF_FLAGS=0x004; OFF_VERSION=0x005; OFF_SUBTYPE=0x006; OFF_DATA_SIZE=0x008
OFF_FILENAME=0x010; OFF_CRYPT_START=0x090; OFF_CRYPT_SPAN=0x098; OFF_IV=0x0A0
OFF_PAYLOAD_SIZE=0x0B0; OFF_SHA256=0x0E0; OFF_RSA=0x100; RSA_SIZE=256
CPIO_FILES=["sw-description","resource","uboot","boot0","kernel","rootfs","cpio_item_md5"]
MD5_FILES=["sw-description","resource","uboot","boot0","kernel","rootfs"]
EXPECTED={
 "stock_swu":"0654a0de241415d5e168fae21faf4384a34dbbf59968ef48dc5102c3e4d1ba9a",
 "sw-description":"6598586ada5e7e9256316c63b801579e65e797d2a77e13ab360f09b3581ae2fb",
 "resource":"aacfe9a9d847f4be3e4b5a16f7c16cba93a60e25ba371efb77568568b0b37aad",
 "uboot":"769d55b96ed4dff486ff5b79f35d61a36d68c444f49f19e44059e2472d468f72",
 "boot0":"b18b3f31b3d378b56b4d01b1863a6b9f0f61211ba1f4ad8e7b672ff54887ec41",
 "kernel":"e1ca036e667f1976fb1a1944b6922df8a1c26d5870912dd8273e0fcc3bb4efec",
 "stock_rootfs":"20375a79e1ee2748f43fd39a62e5a48b23b945ed048dbe52a54d0a1f37b1acae",
 "stock_gui":"24d0e561287a4b5a87c8ec698c98f60acf2c70511114b36224b4eb9249283abd",
 "patched_gui":"5f3122852044f6750dbd36aeb2ebcbd0ea8c5e99f48a52c186def1f66c3ec58a",
 "sshd":"7746b7085a1539f0a8db1aea89a97ea0ed7e8e5e722fecd476fdedfa1e7a002c",
 "sshd_init":"69b42d5f0adf20eeffd5218bc9e955af1d3590d68a07c21ad90128ea7a6930d8",
 "sshd_config":"0b8f561d681101fff24bb5f65f469f6ea8f838d5c12576f104b73c85392eb066",
 "daemon_stock":"6fed6657e814f010fef87b40c5932d8fa7c5e9c044d7376f25fc2318daeb10f9",
 "dual_payload":"04b9f093f439c336476f2a34f109b088b05e6c42a4f1baf2f10e798ed927aa75",
 "daemon_dual":"83433b14e49d1e8aa9523282ead03618378573f4b66de1ad8f7f3c444b11aada",

 "stock_outer":"278a0606a072ae432e9bf2d4f0157acba4570d1c85dbc768c359fef53ac5f406",
 "stock_inner":"5ae3d2e392fa47d7b0bc42981389fd84d702ad069d535c1392d50a7d32ca977b",
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

CC2_CONTROL_SOURCE_COMMIT='24ae9794e15e5d8123591addc3adca6629a489e5'
CC2_CONTROL_SOURCE_SHA256='2587f67829c508a56e2c6cccb86d96bf59d5d8b44712d4ef28341584e0bd0873'
CC2_CONTROL_FILES=('cc2-control','web/index.html','defaults/material-presets.json','start.sh','launch.sh','cc2-control.init','cc2-configure')
CC2_UPDATE_FILES=('cc2-update-worker', 'cc2-https-download', 'update-runner.sh', 'ca-bundle.pem', 'updater-trusted-key.json', 'THIRD_PARTY_NOTICES.txt')
# Translation files are optional and open-ended (one per language), unlike the
# fixed files above; names are validated so only web/locales/<code>.json passes.
CC2_CONTROL_LOCALE=re.compile(r'web/locales/[a-z]{2,5}(-[A-Z]{2})?\.json')

def cc2_control_files(files):
 locales=sorted(k for k in files if k.startswith('web/locales/'))
 if any(not CC2_CONTROL_LOCALE.fullmatch(k) for k in locales):
  raise RuntimeError('Invalid CC2 Control locale file name')
 if locales and 'web/locales/en.json' not in locales:
  raise RuntimeError('CC2 Control locales present without web/locales/en.json')
 updates=tuple(k for k in CC2_UPDATE_FILES if k in files)
 if len(updates)!=len(CC2_UPDATE_FILES):
  raise RuntimeError('Missing or incomplete CC2 Control updater component')
 return CC2_CONTROL_FILES+updates+tuple(locales)

def cc2_control_destinations(root,manifest):
 app=root/'opt/inst/cc2-control'
 destinations={
  'cc2-control':app/'cc2-control', 'web/index.html':app/'web/index.html',
  'defaults/material-presets.json':app/'defaults/material-presets.json',
  'start.sh':app/'start.sh', 'launch.sh':app/'launch.sh',
  'cc2-control.init':root/'etc/init.d/cc2-control', 'cc2-configure':root/'usr/bin/cc2-configure',
 }
 for relative in cc2_control_files(manifest['files'])[len(CC2_CONTROL_FILES):]:
  destinations[relative]=app/relative
 return destinations

def cc2_control_version():
 """VERSION of the pinned source snapshot; the archive hash is checked before it is trusted."""
 source=req(Path(__file__).resolve().parent.parent/'components/cc2-control/source/source.zip','CC2 Control source snapshot')
 reqhash(source,CC2_CONTROL_SOURCE_SHA256,'CC2 Control source snapshot')
 with zipfile.ZipFile(source) as z: return z.read('VERSION').decode('utf-8').strip()

def load_cc2_control(component,manifest_path):
 component=Path(component); manifest_path=Path(manifest_path)
 if not component.is_dir(): raise RuntimeError(f'Missing prepared CC2 Control directory: {component}')
 manifest=json.loads(req(manifest_path,'CC2 Control prepared manifest').read_text(encoding='utf-8'))
 if manifest.get('component')!='CC2 Control' or manifest.get('version')!=cc2_control_version():
  raise RuntimeError('CC2 Control manifest identity mismatch')
 if manifest.get('source_commit')!=CC2_CONTROL_SOURCE_COMMIT:
  raise RuntimeError('CC2 Control source commit mismatch')
 if manifest.get('source_sha256')!=CC2_CONTROL_SOURCE_SHA256:
  raise RuntimeError('CC2 Control source hash mismatch in prepared manifest')
 files=manifest.get('files')
 if not isinstance(files,dict):
  raise RuntimeError('CC2 Control manifest file set mismatch')
 expected=cc2_control_files(files)
 if set(files)!=set(expected):
  raise RuntimeError('CC2 Control manifest file set mismatch')
 for relative in expected:
  item=files[relative]
  if not isinstance(item,dict) or not isinstance(item.get('sha256'),str):
   raise RuntimeError('Invalid CC2 Control manifest entry: '+relative)
  reqhash(component/relative,item['sha256'],'CC2 Control '+relative)
 return component,manifest

def install_cc2_control(root,component,manifest):
 for relative,destination in cc2_control_destinations(root,manifest).items():
  destination.parent.mkdir(parents=True,exist_ok=True)
  shutil.copy2(component/relative,destination)
  os.chmod(destination,int(manifest['files'][relative]['mode'],8))
 rc=root/'etc/rc.d';rc.mkdir(parents=True,exist_ok=True)
 for name in ('S98cc2-control','K10cc2-control'):
  link=rc/name
  if link.exists() or link.is_symlink():link.unlink()
  link.symlink_to('../init.d/cc2-control')

def audit_cc2_control(root,component,manifest):
 for relative,destination in cc2_control_destinations(root,manifest).items():
  reqhash(destination,manifest['files'][relative]['sha256'],'installed CC2 Control '+relative)
 for name in ('S98cc2-control','K10cc2-control'):
  link=root/'etc/rc.d'/name
  if not link.is_symlink() or os.readlink(link)!='../init.d/cc2-control':
   raise RuntimeError(f'Invalid CC2 Control symlink: {link}')

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
 if '02.01.01.07' not in x['filename']: raise RuntimeError(f"Unexpected stock embedded filename: {x['filename']}")
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
 here=Path(__file__).resolve().parent.parent
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
 reqhash(gui,EXPECTED['stock_gui'],'stock GUI')
 module=port_module('patch_zoffset44')
 data=module.patch(Path(gui).read_bytes())
 Path(gui).write_bytes(data); os.chmod(gui,0o775)
 reqhash(gui,EXPECTED['patched_gui'],'patched GUI')

def patch_dual(daemon,payload):
 reqhash(daemon,EXPECTED['daemon_stock'],'stock update daemon')
 reqhash(payload,EXPECTED['dual_payload'],'Dual Trust payload')
 data=port_module('patch_dual44').patch(Path(daemon).read_bytes())
 Path(daemon).write_bytes(data)
 reqhash(daemon,EXPECTED['daemon_dual'],'Dual Trust daemon')


BASE=Path(__file__).resolve().parent.parent
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
 challenge=hashlib.sha256(b'CC2 builder local signing-key check'+os.urandom(32)).digest()
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
HTTP_UPLOAD_MANIFEST=Path(__file__).resolve().parent.parent/'patches/http-upload/http_upload_v1.json'
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


# New vendor executable plus the reviewed HTTP and LAN/WAN patch chain.
PRINTER_RELEASE_SHA256="19712b5943b10bb8cb6aab9a4ed498ee962b28538b23c91b7ed003eb9e590745"
PRINTER_RELEASE_MD5="c29e0574fb1693dbb3c516acbf2ad624"
PRINTER_RELEASE_SIZE=29274740
PRINTER_RELEASE_DEFAULT=BASE/'components/printer/elegoo_printer_44_reference'

def validate_release_printer(data):
 if len(data)!=PRINTER_RELEASE_SIZE or hashlib.sha256(data).hexdigest()!=PRINTER_RELEASE_SHA256 or hashlib.md5(data).hexdigest()!=PRINTER_RELEASE_MD5:
  raise RuntimeError('New vendor patch chain result differs from pinned target')
 return data

def install_release_printer(printer,reference):
 # Optional offline API; normal builds reconstruct directly from the official OTA.
 manifest=json.loads((PORT_DIR/'network44-manifest.json').read_text())
 reqhash(printer,manifest['input_sha256'],'HTTP intermediate')
 Path(printer).write_bytes(validate_release_printer(Path(reference).read_bytes()))

REACTOR_HELPER_SHA256='170851f47569d9f02072403ffda497e3135c1e01574e0a95a543a0f451f31b84'

def firmware_release_version():
 version=(BASE/'RELEASE_VERSION').read_text(encoding='utf-8').strip()
 if not re.fullmatch(re.escape(firmware_version())+r'\.\d+',version):
  raise RuntimeError('Invalid firmware release version for the qualified compatibility family')
 return version

def firmware_version():
 source=BASE/'components/cc2-control/source/source.zip'
 reqhash(source,CC2_CONTROL_SOURCE_SHA256,'CC2 Control source snapshot')
 with zipfile.ZipFile(source) as z: return z.read('FIRMWARE_VERSION').decode().strip()

PORT_DIR=BASE.parents[1]/"experimental/firmware-port"
CAMERA_RELEASE_SHA256="ec5633b17de329d5b18c3f8d8de6ebf389391a04193536d976ba167022d07b99"
PORT_HASHES={'patch_camera44.py': '63ef66a15ed9a12d67fb0c6d919d249da7d5047f4d123cb184580abc06912e63', 'camera44-manifest.json': 'b32224bf3d2901395f8558b2ce4cd4e13d78ee16ee9faffaaf6783bfb49658ab', 'patch_zoffset44.py': '76519510503e2762b2878250a326bc0272c1411042d0627ab106db813a380e10', 'patch_dual44.py': '1281d7ea80e1629fd657678a7d06d9d6333eb4937c4ce092c0ca1425c92b7366', 'dual44-manifest.json': '4d8f775c924e31d1df01b14ad600e687bdb40a452d6fa98fa5a72d2b4b52eefc', 'dual_verify_44.bin': '04b9f093f439c336476f2a34f109b088b05e6c42a4f1baf2f10e798ed927aa75', 'patch_http44.py': '64c9f93dc3f4a52e9153252d184b2d837f1c1bc3a15e3af2ee933fd11f75e915', 'http44-manifest.json': '3e65a1ca3a051dad2cb661b4f6c1b4519a55f733c87e283968968e4a368e3bd4', 'patch_network44.py': '46098ae68477e4791b862ac1aac577b8b5dfcf93497de62e5ae3f7f8bc5cd482', 'network44-manifest.json': '34d90437d1310a3662943ec7b445b5c55fc351d94c6a27108a9f5151357631b9'}

def validate_port_inputs():
 for name,expected in PORT_HASHES.items(): reqhash(PORT_DIR/name,expected,'new OTA port '+name)

def port_module(name):
 validate_port_inputs()
 spec=importlib.util.spec_from_file_location(name,PORT_DIR/(name+'.py'))
 module=importlib.util.module_from_spec(spec);spec.loader.exec_module(module)
 return module

def patch_printer44(printer):
 with tempfile.TemporaryDirectory(prefix='cc2-printer44-') as temporary:
  intermediate=Path(temporary)/'http';final=Path(temporary)/'network'
  port_module('patch_http44').apply(printer,intermediate)
  port_module('patch_network44').apply(intermediate,final)
  reqhash(final,PRINTER_RELEASE_SHA256,'new vendor with HTTP/LAN/WAN patches')
  Path(printer).write_bytes(final.read_bytes())

def main():
 ap=argparse.ArgumentParser(description='CC2 02.01.01.07 Dual-Trust firmware builder with CC2 Control and Panda compatibility')
 here=Path(__file__).resolve().parent.parent
 ap.add_argument('stock_package',nargs='?',default=str(here/'original_firmware'/'cc2_eeb001_02.01.01.07_20260922210235.zip.sig'),help='official stock .zip.sig (default: original_firmware/...)')
 ap.add_argument('--signing-mode',choices=['stock','community'],default='community',help='stock: initial install; community: later updates')
 ap.add_argument('--private-key',help='Private key for selected mode; explicit path required in stock mode')
 ap.add_argument('--check-signing-key-only',action='store_true',help='Check key pair and sign/verify a local challenge; no firmware or AES required')
 ap.add_argument('--aes-key',default=str(here/'keys'/'cc2_aes_key_v1.bin'))
 ap.add_argument('--toolchain-manifest',help='Explicit local build manifest; hashes checked in addition to version and round-trip')
 ap.add_argument('--mksquashfs'); ap.add_argument('--unsquashfs')
 ap.add_argument('--cc2-control-dir',default=str(here/'components/cc2-control/prepared'),help='Prepared CC2 Control component directory')
 ap.add_argument('--cc2-control-manifest',default=str(here/'components/cc2-control/prepared-manifest.json'),help='Prepared CC2 Control manifest')
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
 fw_tag=firmware_release_version().replace('.','_')
 a.output=a.output or ('CC2_FULL_V'+fw_tag+'_'+release_tag+'.zip.sig')
 stock_pkg=req(a.stock_package,'official stock .zip.sig')
 aes_path=req(a.aes_key,'AES key'); reqhash(aes_path,EXPECTED['aes_key'],'AES key v1'); aes=aes_path.read_bytes()
 if len(aes)!=32: raise RuntimeError('AES key must be 32 bytes')
 sshd=req(a.sshd,'sshd'); init=req(a.sshd_init,'sshd.init'); conf=req(a.sshd_config,'sshd_config'); dual=req(a.dual_payload,'dual_verify.bin')
 reqhash(sshd,EXPECTED['sshd'],'sshd'); reqhash(init,EXPECTED['sshd_init'],'sshd.init'); reqhash(conf,EXPECTED['sshd_config'],'sshd_config'); reqhash(dual,EXPECTED['dual_payload'],'Dual Trust payload')
 zpatch=req(a.zoffset_patcher,'Z-offset patcher'); dpatch=req(a.dual_patcher,'Dual Trust patcher'); sigtool=req(a.sig_tool,'cc2_sig_tool_v1.1.py')
 reqhash(zpatch,EXPECTED['zoffset_patcher'],'Z-offset patcher'); reqhash(dpatch,EXPECTED['dual_patcher'],'Dual Trust patcher'); reqhash(sigtool,EXPECTED['sig_tool'],'cc2_sig_tool v1.1')
 validate_port_inputs()
 cc2_component,cc2_manifest=load_cc2_control(a.cc2_control_dir,a.cc2_control_manifest)
 reactor_helper=here/'core/reactor_component.py'
 reqhash(reactor_helper,REACTOR_HELPER_SHA256,'Reactor integration helper')
 spec=importlib.util.spec_from_file_location('reactor_component',reactor_helper)
 reactor=importlib.util.module_from_spec(spec); spec.loader.exec_module(reactor)
 reactor_component=here/'components/reactor'
 reactor.load(reactor_component)
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
  reqhash(printer,'8039081f9c4b01e720d838d4ab6e4515a8979b9a71a33bd4164321c282e06074','stock elegoo_printer')
  reqhash(daemon,EXPECTED['daemon_stock'],'stock update daemon')
  patch_zoffset(gui)
  patch_printer44(printer)
  camera=root/'opt/bin/ai_camera'
  camera_patched=work/'ai_camera.coexist'
  port_module('patch_camera44').apply(camera,camera_patched)
  camera.write_bytes(camera_patched.read_bytes())
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
  install_cc2_control(root,cc2_component,cc2_manifest)
  reactor.install(root,reactor_component)
  # Verify intended rootfs surface before compression.
  reqhash(gui,EXPECTED['patched_gui'],'installed patched GUI'); reqhash(root/'usr/sbin/sshd',EXPECTED['sshd'],'installed sshd')
  reqhash(camera,CAMERA_RELEASE_SHA256,'installed coexistence camera')
  reqhash(printer,PRINTER_RELEASE_SHA256,'installed patched elegoo_printer')
  reqhash(daemon,EXPECTED['daemon_dual'],'installed Dual Trust daemon')
  audit_cc2_control(root,cc2_component,cc2_manifest)
  reactor.audit(root,reactor_component)
  banner('REBUILD SQUASHFS'); new=work/'rootfs.new'; log=run([mk,root,new,'-comp','xz','-b','262144','-noappend','-no-tailends','-exports','-all-root'],capture=True)
  if 'Failed to read file' in log: raise RuntimeError('mksquashfs reported Failed to read file')
  shutil.copyfile(new,cpio/'rootfs'); root_hash=sha256(cpio/'rootfs'); print('rootfs SHA256:',root_hash)
  g_root=golden_report('rebuilt rootfs image (byte-level; metadata may differ)',cpio/'rootfs','rootfs') if (a.golden_check or a.require_golden) else None
  # Re-extract and audit the critical files after SquashFS creation.
  vr=work/'verify_rootfs'; run([un,'-d',vr,cpio/'rootfs'],capture=True)
  reqhash(vr/'opt/bin/ec-eeb001-gui',EXPECTED['patched_gui'],'rebuilt patched GUI'); reqhash(vr/'usr/sbin/sshd',EXPECTED['sshd'],'rebuilt sshd')
  reqhash(vr/'opt/bin/ai_camera',CAMERA_RELEASE_SHA256,'rebuilt coexistence camera')
  reqhash(vr/'opt/bin/elegoo_printer',PRINTER_RELEASE_SHA256,'rebuilt patched elegoo_printer')
  reqhash(vr/'opt/inst/daemon-000/daemon-000',EXPECTED['daemon_dual'],'rebuilt Dual Trust daemon')
  audit_cc2_control(vr,cc2_component,cc2_manifest)
  reactor.audit(vr,reactor_component)
  reqhash(vr/'etc/init.d/sshd',EXPECTED['sshd_init'],'rebuilt sshd init')
  reqhash(vr/'etc/ssh/sshd_config',EXPECTED['sshd_config'],'rebuilt sshd config')
  for n in ('S50sshd','K50sshd'):
   p=vr/'etc/rc.d'/n
   if not p.is_symlink() or os.readlink(p)!='../init.d/sshd': raise RuntimeError(f'Invalid rebuilt SSH symlink: {p}')
  banner('REBUILD SWU'); data=''.join(f'{md5(cpio/n)}  {n}\n' for n in MD5_FILES).encode('ascii')
  if len(data)!=254: raise RuntimeError('cpio_item_md5 format mismatch')
  (cpio/'cpio_item_md5').write_bytes(data); swu=work/('CC2_02.01.01.07_V'+fw_tag+'_CC2_CONTROL.swu')
  # Match the historical WSL1-safe build path: real printf | cpio pipeline.
  # Restore the metadata used by the validated historical 02.01.01.07 build.
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
  swusig_name='CC2_02.01.01.07_V'+fw_tag+'_CC2_CONTROL_'+release_tag+'.swu.sig'; swusig=work/swusig_name
  eleg_encrypted(swu.read_bytes(),swusig,aes,priv,swusig_name[:-4],0x80); check_eleg(swusig,0x80)
  swusig_hash=sha256(swusig)
  g_swusig=golden_report('SWU.SIG',swusig,'swusig') if (a.golden_check or a.require_golden) else None
  manifest_obj={"packages":[{"file":swusig_name,"hash":swusig_hash}],"version":"02.01.01.07","update_class":"00.00.00.00"}
  manifest=json.dumps(manifest_obj,separators=(',',':')).encode('utf-8'); mansig=work/'ota-package-list.json.sig'
  eleg_encrypted(manifest,mansig,aes,priv,'ota-package-list.json',0x83); check_eleg(mansig,0x83)
  z=work/('CC2_FULL_V'+fw_tag+'_'+release_tag+'.zip')
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
   # Rebuilt SquashFS/SWU/ELEG byte hashes are not deterministic because
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
