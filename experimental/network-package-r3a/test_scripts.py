import pathlib,tempfile,subprocess,os,re,hashlib
root=pathlib.Path(__file__).resolve().parent
old=(root/'old-remote-command.txt').read_text()
r=subprocess.run(['sh','-n','-c',old],capture_output=True)
assert r.returncode!=0, 'old native-argument corruption did not reproduce the syntax error'
for name in ('prepare-disk.sh','verify-and-run.sh'):subprocess.run(['sh','-n',str(root/name)],check=True)
with tempfile.TemporaryDirectory(prefix='cc2-prepare-') as tmp:
 tmp=pathlib.Path(tmp);stage=tmp/'stage';bin=tmp/'bin';bin.mkdir()
 for name,text in {'id':'#!/bin/sh\necho 0\n','df':'#!/bin/sh\nprintf "Filesystem blocks used available capacity mount\\nfixture 100000 100 99900 1%% /opt/usr\\n"\n'}.items():
  p=bin/name;p.write_text(text);p.chmod(0o700)
 env=dict(os.environ,PATH=str(bin)+':'+os.environ['PATH'])
 script=tmp/'prepare.sh';script.write_text((root/'prepare-disk.sh').read_text().replace('/opt/usr/cc2-control-network-trial-r3',str(stage)))
 subprocess.run(['sh',str(script)],env=env,check=True,capture_output=True)
 assert stage.is_dir() and stage.stat().st_mode&0o777==0o700
 (stage/'sentinel').write_text('retain')
 r=subprocess.run(['sh',str(script)],env=env,capture_output=True)
 assert r.returncode!=0 and (stage/'sentinel').read_text()=='retain'
 runner=stage/'run-install.sh';runner.write_text('#!/bin/sh\nexit 9\n')
 digest=hashlib.sha256(runner.read_bytes()).hexdigest()
 verify=(root/'verify-and-run.sh').read_text().replace('/opt/usr/cc2-control-network-trial-r3',str(stage))
 verify,count=re.subn(r' = [a-f0-9]{64} ]',' = '+digest+' ]',verify);assert count==1
 p=stage/'verify.sh';p.write_text(verify)
 r=subprocess.run(['sh',str(p)],capture_output=True);assert r.returncode==9
 runner.write_text('#!/bin/sh\nexit 0\n')
 r=subprocess.run(['sh',str(p)],capture_output=True);assert r.returncode==1 and b'runner mismatch' in r.stdout
print('PASS: old Windows quoting regression, new script syntax, private disk preparation, retained-stage refusal, runner hash and exit preservation')
