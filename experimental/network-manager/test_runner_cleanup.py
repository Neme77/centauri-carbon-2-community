"""Exercise the exact disk runner: delete only verified successful staging."""
import pathlib,tempfile,tarfile,hashlib,subprocess,re,io
root=pathlib.Path(__file__).resolve().parent
original=(root/'run-install.sh.in').read_text()
for scenario in ('success','failure','changed_archive','symlink_payload'):
 with tempfile.TemporaryDirectory(prefix='cc2-cleanup-') as tmp:
  parent=pathlib.Path(tmp);stage=parent/'stage';stage.mkdir()
  backup=parent/'rollback';backup.mkdir();(backup/'restore.sh').write_text('preserve')
  payload=stage/'cc2-control-payload.tar.gz'
  code=7 if scenario=='failure' else 0
  script='#!/bin/sh\nprintf "INSTALL_PHASE=startup-verification\\n"\n'
  if scenario=='changed_archive':script+='printf changed >> "'+str(payload)+'"\n'
  script+='exit '+str(code)+'\n'
  with tarfile.open(payload,'w:gz') as archive:
   for name,data in {'install-on-printer.sh':script.encode(),'cc2-control':b'fixture'}.items():
    entry=tarfile.TarInfo(name);entry.size=len(data);entry.mode=0o700;archive.addfile(entry,io.BytesIO(data))
  digest=hashlib.sha256(payload.read_bytes()).hexdigest()
  text=original.replace('/opt/usr/cc2-control-network-trial-r4',str(stage))
  text,count=re.subn(r' = [a-f0-9]{64} ]',' = '+digest+' ]',text)
  assert count==2
  runner=stage/'run-install.sh';runner.write_text(text)
  if scenario=='symlink_payload':(stage/'payload').symlink_to(backup,target_is_directory=True)
  result=subprocess.run(['sh',str(runner)],capture_output=True,timeout=10)
  assert (backup/'restore.sh').read_text()=='preserve'
  if scenario=='symlink_payload':
   assert result.returncode!=0 and payload.exists();continue
  assert result.returncode==code,(scenario,result.returncode,result.stdout,result.stderr)
  assert (stage/'exit.code').read_text().strip()==str(code)
  log=(stage/'install.log').read_text();assert 'INSTALL_PHASE=startup-verification' in log
  if scenario=='success':
   assert not payload.exists() and not (stage/'payload').exists()
   assert 'CLEANUP PASSED' in log
  else:
   assert payload.exists() and (stage/'payload/install-on-printer.sh').exists()
   assert 'CLEANUP SKIPPED' in log
print('PASS: success removes archive/extraction; failure or changed identity retains them; backup/log retained; symlink refused')
