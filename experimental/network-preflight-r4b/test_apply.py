"""Exercise the exact correction script, including restoration on failure."""
from pathlib import Path
import hashlib, os, subprocess, tempfile
root=Path(__file__).resolve().parent
sha=lambda b:hashlib.sha256(b).hexdigest()
template=(root/'apply.sh').read_text()
original_manifest=(root/'SHA256SUMS.before').read_text()
fixed_manifest=(root/'SHA256SUMS.after').read_text()
manager=(root/'manage.reference.sh').read_bytes()
assert sha(manager)=='5f548f24b150eda793a2e7c712a8e6f7a601f2238517b591660ed2df753df0c4'
before=(root/'preflight.before.sh').read_bytes();after=(root/'preflight.sh').read_bytes()
for case in ('success','changed-manifest','active-watchdog','rollback'):
 with tempfile.TemporaryDirectory(prefix='cc2-r4b-apply-') as tmp:
  tmp=Path(tmp);base=tmp/'base';base.mkdir();work=tmp/'backup';proc=tmp/'proc';proc.mkdir()
  files={line.split('  ',1)[1]:b'fixture' for line in original_manifest.splitlines()}
  files.update({'manage.sh':manager,'preflight.sh':before,'source/preflight.sh':before})
  manifest=lambda data:''.join(sha(data[n])+'  '+n+'\n' for n in sorted(data))
  initial=manifest(files);fixed=dict(files,**{'preflight.sh':after,'source/preflight.sh':after});final=manifest(fixed)
  for n,b in files.items():
   p=base/n;p.parent.mkdir(parents=True,exist_ok=True);p.write_bytes(b)
  (base/'SHA256SUMS').write_text(initial)
  mock=tmp/'bin';mock.mkdir();p=mock/'id';p.write_text('#!/bin/sh\necho 0\n');p.chmod(0o700)
  script=template
  script=script.replace(fixed_manifest,final).replace(sha(original_manifest.encode()),sha(initial.encode())).replace(sha(fixed_manifest.encode()),sha(final.encode()))
  script=script.replace('/tmp/cc2-network-compat-r1',str(base)).replace('/opt/usr/cc2-network-preflight-r4b',str(work)).replace('/proc/[0-9]*/cmdline',str(proc)+'/[0-9]*/cmdline')
  if case=='changed-manifest':(base/'SHA256SUMS').write_text('changed\n')
  if case=='active-watchdog':
   (proc/'123').mkdir();(proc/'123/cmdline').write_bytes(('sh\0'+str(base)+'/manage.sh\0__watch\0').encode())
  if case=='rollback':
   exact='(cd "$BASE" && sha256sum -c SHA256SUMS)'
   assert script.count(exact)==2
   left,right=script.rsplit(exact,1);script=left+'false'+right
  p=tmp/'apply.sh';p.write_text(script)
  result=subprocess.run(os.environ.get('TEST_SHELL','sh').split()+[str(p)],env=dict(os.environ,PATH=str(mock)+':'+os.environ['PATH']),capture_output=True,text=True,timeout=10)
  assert (result.returncode==0)==(case=='success'),(case,result.stdout,result.stderr)
  for n,b in files.items():
   assert (base/n).read_bytes()==(fixed[n] if case=='success' else b),case
  if case=='success':
   assert (base/'SHA256SUMS').read_text()==final
   assert (work/'preflight.before').read_bytes()==before
   assert 'PATCH PASSED' in (work/'result.log').read_text()
  elif case=='rollback':assert (base/'SHA256SUMS').read_text()==initial
  else:assert not work.exists()
print('PASS: exact hotfix apply, backup, changed-manifest refusal, watchdog refusal and rollback; other payload files unchanged')
