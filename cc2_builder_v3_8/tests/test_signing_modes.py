"""Real community/stock-verification tests; temporary RSA fixture for stock signing.
The real stock private key is never required or included in this test.
"""
import argparse,hashlib,importlib.util,subprocess,tempfile
from pathlib import Path
BASE=Path(__file__).resolve().parents[1]
s=importlib.util.spec_from_file_location('builder',BASE/'cc2_firmware_builder_v3.8.py');b=importlib.util.module_from_spec(s);s.loader.exec_module(b)
def reject(fn):
 try:fn()
 except RuntimeError:return
 raise AssertionError('Expected rejection')
def main():
 p=argparse.ArgumentParser();p.add_argument('--community-private',required=True);p.add_argument('--stock-package',required=True);p.add_argument('--derived-stock-public',required=True);a=p.parse_args()
 assert b.public_der(a.derived_stock_public)==b.public_der(b.STOCK_PUBLIC)
 b.verify_signature(a.stock_package,b.STOCK_PUBLIC)
 b.verify_signing_key(a.community_private,'community')
 reject(lambda:b.verify_signing_key(a.community_private,'stock'))
 print('PASS: real stock public matches derived key; original signature valid; community private rejected for stock mode')
 r=subprocess.run(['python3',str(BASE/'cc2_firmware_builder_v3.8.py'),'--signing-mode','stock','--check-signing-key-only'],capture_output=True,text=True)
 assert r.returncode!=0 and 'requires --private-key' in r.stderr
 old_path=b.STOCK_PUBLIC;old_hash=b.PUBLIC_HASHES['stock']
 with tempfile.TemporaryDirectory() as td:
  t=Path(td);private=t/'fixture-private.pem';public=t/'fixture-public.pem'
  subprocess.run(['openssl','genpkey','-algorithm','RSA','-pkeyopt','rsa_keygen_bits:2048','-out',str(private)],check=True,capture_output=True)
  subprocess.run(['openssl','pkey','-in',str(private),'-pubout','-out',str(public)],check=True,capture_output=True)
  # Test-only substitution in the imported module. No production override is added.
  b.STOCK_PUBLIC=public;b.PUBLIC_HASHES['stock']=hashlib.sha256(public.read_bytes()).hexdigest()
  try:
   b.verify_signing_key(private,'stock')
   reject(lambda:b.verify_signing_key(private,'community'))
   for mode,priv,pub,other in [('stock',private,public,b.COMMUNITY_PUBLIC),('community',a.community_private,b.COMMUNITY_PUBLIC,public)]:
    for typ in [4,128,131]:
     f=t/f'{mode}-{typ}.sig'
     if typ==4:b.eleg_plain(b'fixture payload',f,priv,'test.zip')
     else:b.eleg_encrypted(b'fixture payload',f,bytes(32),priv,'test',typ)
     b.validate_eleg(f,typ,encrypted=typ!=4);b.verify_signature(f,pub)
     reject(lambda:b.verify_signature(f,other))
     data=bytearray(f.read_bytes());data[256]^=1;f.write_bytes(data)
     reject(lambda:b.verify_signature(f,pub))
   print('PASS: stock-mode signing with temporary RSA fixture and real community mode; all three roles; cross-key and altered-signature rejection')
  finally:b.STOCK_PUBLIC=old_path;b.PUBLIC_HASHES['stock']=old_hash
 print('NOT TESTED: signing with real stock private key; complete OTA build/install on stock board')
if __name__=='__main__':main()
