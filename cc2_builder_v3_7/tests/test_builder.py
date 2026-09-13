"""Host tests. Inputs remain outside the release; no private keys are copied."""
import argparse,importlib.util,tempfile,subprocess,hashlib,contextlib,io
from pathlib import Path
BASE=Path(__file__).resolve().parents[1]
spec=importlib.util.spec_from_file_location('builder',BASE/'cc2_firmware_builder_v3.7.py'); m=importlib.util.module_from_spec(spec);spec.loader.exec_module(m)
def rejected(fn):
 try: fn()
 except RuntimeError:return
 raise AssertionError('Expected rejection')
def main():
 ap=argparse.ArgumentParser();ap.add_argument('--private-key',required=True);ap.add_argument('--stock-daemon',required=True);ap.add_argument('--stock-package');ap.add_argument('--aes-key');a=ap.parse_args()
 with tempfile.TemporaryDirectory() as td:
  p=Path(td)
  m.verify_signing_key(a.private_key)
  wrong=p/'wrong.pem'
  subprocess.run(['openssl','genpkey','-algorithm','RSA','-pkeyopt','rsa_keygen_bits:2048','-out',str(wrong)],check=True,stdout=subprocess.DEVNULL,stderr=subprocess.DEVNULL)
  rejected(lambda:m.verify_signing_key(wrong))
  print('signing key match/mismatch PASS')
  for typ in [4,128,131]:
   f=p/f'{typ}.sig'
   if typ==4:m.eleg_plain(b'payload-test',f,a.private_key,'test.zip')
   else:m.eleg_encrypted(b'payload-test',f,bytes(32),a.private_key,'test',typ)
   m.validate_eleg(f,typ,encrypted=typ!=4);m.verify_signature(f,m.COMMUNITY_PUBLIC)
   original=f.read_bytes();bad=bytearray(original);bad[256]^=1;f.write_bytes(bad)
   rejected(lambda:m.verify_signature(f,m.COMMUNITY_PUBLIC))
   bad=bytearray(original);bad[-1]^=1;f.write_bytes(bad)
   rejected(lambda:m.verify_signature(f,m.COMMUNITY_PUBLIC))
  print('three ELEG roles: valid / corrupt signature / corrupt payload PASS')
  dst=p/'daemon';dst.write_bytes(Path(a.stock_daemon).read_bytes());m.patch_dual(dst,BASE/'dualtrust/dual_verify.bin')
  assert dst.read_bytes()==(BASE/'dualtrust/daemon-000-dualtrust-v2').read_bytes()
  rejected(lambda:m.patch_dual(dst,BASE/'dualtrust/dual_verify.bin'))
  print('builder daemon exact reproduction and wrong input rejection PASS')
  tool=p/'tool';tool.write_bytes(b'wrong');manifest=p/'manifest.json';manifest.write_text('{"source_commit":"d8cb82d9840330f9344ec37b992595b5d7b44184","sha256":{"mksquashfs":"invalid"}}')
  rejected(lambda:m.verify_squashfs(tool,tool,p,manifest));print('toolchain hash mismatch PASS')
  if a.stock_package:
   assert a.aes_key
   m.decode_stock_package(Path(a.stock_package),Path(a.aes_key).read_bytes(),p)
   print('original package RSA chain + decrypt/hash PASS')
if __name__=='__main__':main()
