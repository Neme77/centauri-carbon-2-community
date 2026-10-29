import pathlib,tempfile,tarfile,hashlib,subprocess,re,io
root=pathlib.Path(__file__).resolve().parent
original=(root/'run-install.sh').read_text()
for code in (0,7):
 with tempfile.TemporaryDirectory(prefix='cc2-runner-') as tmp:
  stage=pathlib.Path(tmp)/'stage';stage.mkdir()
  archive=stage/'cc2-control-payload.tar.gz'
  with tarfile.open(archive,'w:gz') as t:
   for name,data in {'install-on-printer.sh':('#!/bin/sh\nprintf "INSTALL_PHASE=fixture\\n"\nexit '+str(code)+'\n').encode(),'cc2-control':b'fixture'}.items():
    e=tarfile.TarInfo(name);e.size=len(data);e.mode=0o700;t.addfile(e,io.BytesIO(data))
  text=original.replace('/opt/usr/cc2-control-network-trial-r3',str(stage))
  digest=hashlib.sha256(archive.read_bytes()).hexdigest()
  text,count=re.subn(r' = [a-f0-9]{64} ]',' = '+digest+' ]',text)
  assert count==1
  runner=stage/'run-install.sh';runner.write_text(text)
  r=subprocess.run(['sh',str(runner)],capture_output=True,timeout=10)
  assert r.returncode==code,(code,r.returncode,r.stdout,r.stderr)
  assert (stage/'exit.code').read_text().strip()==str(code)
  assert 'INSTALL_PHASE=fixture' in (stage/'install.log').read_text()
  assert ('INSTALL_COMMAND_EXIT='+str(code)).encode() in r.stdout
print('PASS: same disk runner preserves success/failure exit through tee and retains its log')
