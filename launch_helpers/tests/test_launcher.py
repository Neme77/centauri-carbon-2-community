from pathlib import Path
import importlib.util,tempfile,io,hashlib,contextlib
from unittest.mock import patch
ROOT=Path(__file__).resolve().parents[2]
s=importlib.util.spec_from_file_location('launcher',ROOT/'launch_helpers/run_build.py');m=importlib.util.module_from_spec(s);s.loader.exec_module(m)
with tempfile.TemporaryDirectory(prefix='cc2 launch test ') as td:
 r=Path(td)
 builder=r/'cc2_builder_v3_7/cc2_firmware_builder_v3.7.py';builder.parent.mkdir();builder.write_bytes((ROOT/'cc2_builder_v3_7/cc2_firmware_builder_v3.7.py').read_bytes())
 for name in ['keys/cc2_stock_private.pem','keys/cc2_community_release_private.pem','keys/cc2_aes_key_v1.bin','original_firmware/cc2_eeb001_02.01.00.00_20260707170825.zip.sig','tools/squashfs-tools-4.6.1/bin/mksquashfs','tools/squashfs-tools-4.6.1/bin/unsquashfs']:
  p=r/name;p.parent.mkdir(parents=True,exist_ok=True);p.write_text('TEST PLACEHOLDER')
 for mode in ['stock','community']:
  for pre,key in [(False,False),(True,False),(False,True)]:
   cmd,out,log=m.build_arguments(r,mode,pre,key)
   assert cmd[cmd.index('--signing-mode')+1]==mode
   assert cmd[cmd.index('--private-key')+1]==str(r/('keys/cc2_stock_private.pem' if mode=='stock' else 'keys/cc2_community_release_private.pem'))
   assert (out is None)==(pre or key)
   if key:assert '--aes-key' not in cmd and '--check-signing-key-only' in cmd
   if pre:assert '--preflight-only' in cmd
   assert ' ' in str(r) and str(builder) in cmd
 try:m.build_arguments(r,'stock',True,True)
 except ValueError:pass
 else:raise AssertionError('mutually exclusive controls accepted')
 class Fake:
  def __init__(self,cmd,rc=0,**kw):
   self.stdout=io.StringIO('SIMULATED BUILDER OUTPUT\n');self.rc=rc
   if not rc and '--output' in cmd:Path(cmd[cmd.index('--output')+1]).write_bytes(b'TEST ONLY')
  def wait(self,**kw):return self.rc
 with patch.object(m.subprocess,'Popen',Fake),contextlib.redirect_stdout(io.StringIO()):
  assert m.run(r,'stock')==0
  assert m.run(r,'stock')==0
  assert m.run(r,'community',check_key=True)==0
 assert len(list((r/'output').glob('*.zip.sig')))==2
 assert len(list((r/'output').glob('*.sha256.txt')))==2
 with patch.object(m.subprocess,'Popen',lambda cmd,**kw:Fake(cmd,rc=9,**kw)),contextlib.redirect_stdout(io.StringIO()),contextlib.redirect_stderr(io.StringIO()):assert m.run(r,'community')==9
 assert len(list((r/'output').glob('*.zip.sig')))==2
 builder.write_text('CHANGED')
 try:m.build_arguments(r,'stock')
 except RuntimeError:pass
 else:raise AssertionError('changed builder accepted')
print('PASS: both modes, fixed key paths, spaces, key/preflight/build flags, unique outputs/logs/checksums, simulated failure propagation, changed-builder refusal.')
print('PowerShell/WSL invocation not executed: Windows/PowerShell unavailable in this environment.')
