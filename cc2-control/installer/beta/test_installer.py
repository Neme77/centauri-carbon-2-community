import tempfile, pathlib, shutil, subprocess, hashlib, os
import sys
ROOT=pathlib.Path(sys.argv[1]).resolve()
EXPECTED=hashlib.sha256((ROOT/'payload/cc2-control').read_bytes()).hexdigest()
def run(case):
 with tempfile.TemporaryDirectory() as d:
  d=pathlib.Path(d); stage=d/'stage';shutil.copytree(ROOT/'payload',stage)
  target=d/'usr/cc2-control';target.mkdir(parents=True)
  (target/'cc2-control').write_text('original binary')
  (target/'cc2-control.conf').write_text('configuration unchanged')
  (target/'ui-preferences.json').write_text('preferences unchanged')
  mock=d/'mock';mock.mkdir();state=d/'running'
  init=d/'init';init.write_text(f'#!/bin/sh\ncase "$1" in stop) rm -f "{state}";; start) touch "{state}";; esac\n');init.chmod(0o755)
  (stage/'cc2-control.init').write_text(init.read_text())
  state.touch()
  body=(stage/'install-on-printer.sh').read_text().replace('/opt/usr',str(d/'usr')).replace('/etc/init.d/cc2-control',str(init)).replace('/tmp/cc2-control-beta-install.lock',str(d/'lock')).replace('/proc/$RUN_PID/exe',str(target/'cc2-control'))
  (stage/'install-on-printer.sh').write_text(body)
  status='{"connected":true,"last_message_age":0,"machine":{"status":1,"status_name":"Idle"}}'
  if case=='busy': status=status.replace('"status":1','"status":2')
  if case=='stale':status=status.replace('"last_message_age":0','"last_message_age":99')
  health='{"service":"cc2-control","version":"1.1.31","mqtt_registered":true}'
  if case=='health_failure':health='{}'
  (mock/'wget').write_text(f'#!/bin/sh\ncase "$*" in */api/printer*) echo \'{status}\';; *) echo \'{health}\';; esac\n')
  (mock/'pidof').write_text(f'#!/bin/sh\n[ -f "{state}" ] || exit 1\necho 7\n')
  (mock/'sleep').write_text('#!/bin/sh\nexit 0\n')
  if case=='copy_failure':
   (mock/'cp').write_text(f'#!/bin/sh\ncase "$*" in *"{stage}/start.sh"*) exit 1;; esac\nexec /bin/cp "$@"\n')
  for p in mock.iterdir():p.chmod(0o755)
  files=sorted(p for p in stage.rglob('*') if p.is_file() and p.name!='SHA256SUMS')
  (stage/'SHA256SUMS').write_text(''.join(f'{hashlib.sha256(p.read_bytes()).hexdigest()}  {p.relative_to(stage)}\n' for p in files))
  if case=='corrupt': (stage/'web/index.html').write_text('corrupted')
  env=dict(os.environ,PATH=str(mock)+':'+os.environ['PATH'])
  result=subprocess.run(['sh',str(stage/'install-on-printer.sh')],env=env,capture_output=True,text=True)
  assert (result.returncode==0)==(case=='success'),(case,result.stdout,result.stderr)
  assert (target/'cc2-control.conf').read_text()=='configuration unchanged'
  assert (target/'ui-preferences.json').read_text()=='preferences unchanged'
  if case=='success':
   assert hashlib.sha256((target/'cc2-control').read_bytes()).hexdigest()==EXPECTED
   assert (target/'web/locales/zh.json').exists()
   assert len(list((d/'usr').glob('cc2-control-backup-beta-*')))==1
  else:
   assert (target/'cc2-control').read_text()=='original binary'
   assert state.exists()
  print('PASS',case)
for case in ['success','busy','stale','corrupt','copy_failure','health_failure']:run(case)
