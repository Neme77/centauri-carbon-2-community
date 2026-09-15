"""Exact patch reproduction, input rejection, and combined-component checks."""
import argparse,hashlib,importlib.util,tempfile,json
from pathlib import Path
BASE=Path(__file__).resolve().parents[1]
spec=importlib.util.spec_from_file_location('builder',BASE/'cc2_firmware_builder_v3.8.py');b=importlib.util.module_from_spec(spec);spec.loader.exec_module(b)
def reject(fn):
 try:fn()
 except RuntimeError:return
 raise AssertionError('Expected refusal')
def main():
 p=argparse.ArgumentParser();p.add_argument('--stock-printer',required=True);p.add_argument('--combined-v1',required=True);p.add_argument('--stock-gui',required=True);p.add_argument('--stock-daemon',required=True);a=p.parse_args()
 original=Path(a.stock_printer).read_bytes();expected=Path(a.combined_v1).read_bytes()
 patched=b.patch_http_upload_bytes(original)
 assert patched==expected
 assert hashlib.md5(patched).hexdigest()=='b3a607b6d1db7b3d4224c2b3b5f0341a'
 print('PASS: exact byte-for-byte combined v1 reported tested with OrcaSlicer LAN')
 # V2 WAN entrypoint and cleanup hooks must remain stock.
 for va in [0x5c0e48,0x5c1118,0x5c12d0]:
  off=va-0x10000;assert patched[off:off+4]==original[off:off+4]
 print('PASS: upload v2 WAN hooks absent')
 reject(lambda:b.patch_http_upload_bytes(patched))
 corrupt=bytearray(original);corrupt[0x5e2ff0]^=1
 reject(lambda:b.patch_http_upload_bytes(corrupt));reject(lambda:b.patch_http_upload_bytes(b'truncated'))
 print('PASS: modified/unknown/truncated input refused')
 with tempfile.TemporaryDirectory() as td:
  root=Path(td);manifest=root/'wrong.json';manifest.write_bytes(b.HTTP_UPLOAD_MANIFEST.read_bytes()+b' ')
  reject(lambda:b.patch_http_upload_bytes(original,manifest))
  printer=root/'elegoo_printer';printer.write_bytes(original);printer.chmod(0o775)
  gui=root/'gui';gui.write_bytes(Path(a.stock_gui).read_bytes())
  daemon=root/'daemon';daemon.write_bytes(Path(a.stock_daemon).read_bytes())
  b.patch_zoffset(gui);b.patch_dual(daemon,BASE/'dualtrust/dual_verify.bin');b.patch_http_upload(printer)
  assert printer.read_bytes()==expected and printer.stat().st_mode&0o777==0o775
  assert b.sha256(gui)==b.EXPECTED['patched_gui'] and b.sha256(daemon)==b.EXPECTED['daemon_dual']
  before=printer.read_bytes();reject(lambda:b.patch_http_upload(printer));assert printer.read_bytes()==before
  print('PASS: GUI Z-offset + daemon dual trust v2 + printer HTTP/upload v1; mode preserved; failed reapply leaves bytes unchanged')
if __name__=='__main__':main()
