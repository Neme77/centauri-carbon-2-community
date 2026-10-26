"""Run authenticated-installer camera hooks with isolated filesystem/service doubles."""
import hashlib
import importlib.util
import io
import os
from pathlib import Path
import subprocess
import tarfile
import tempfile
import zipfile

ROOT=Path(__file__).resolve().parents[1]
spec=importlib.util.spec_from_file_location('camera_update_package',ROOT/'installer/package.py')
pkg=importlib.util.module_from_spec(spec);spec.loader.exec_module(pkg)
import camera_component
sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()

with tempfile.TemporaryDirectory(prefix='cc2-camera-transaction-') as temporary:
 for mode in ('success','preflight-fail','health-fail','install-fail','commit-fail','foreign-edit'):
  root=Path(temporary)/mode;root.mkdir();stage=root/'stage';stage.mkdir()
  target=root/'opt/usr/cc2-control';target.mkdir(parents=True)
  (target/'cc2-control').write_bytes(b'old-app')
  camera_init=root/'printer';camera_init.write_text('#!/bin/sh\n# original printer startup\n');original=camera_init.read_bytes()
  component=root/'component';component.mkdir()
  # The real generated octal-embedded archive is extracted by the installer.
  # Only the component's hardware/filesystem boundary is replaced here.
  (component/'manage.sh').write_text('''#!/bin/sh
set -eu
case "$1" in
check) [ "$CAMERA_CASE" != preflight-fail ];;
install)
 [ "$CAMERA_CASE" != install-fail ] || exit 1
 mkdir -p "$CAMERA_TARGET/camera"
 printf '#!/bin/sh\\n# camera hook installed\\n' > "$CAMERA_INIT"
 sha256sum "$CAMERA_INIT" | awk '{print $1}' > "$CAMERA_TARGET/camera/patched.sha256"
 if [ "$CAMERA_CASE" = foreign-edit ];then echo '# unrelated change' >> "$CAMERA_INIT";exit 1;fi
 [ "$CAMERA_CASE" != commit-fail ];;
esac
''')
  for name in ('common.sh','boot-camera.sh'):(component/name).write_text('#!/bin/sh\nexit 0\n')
  native=root/'native';native.mkdir()
  for name in camera_component.PINS:(native/name).write_bytes(b'camera-fixture-'+name.encode())
  saved_pins=camera_component.PINS
  camera_component.PINS={name:sha(native/name) for name in saved_pins}
  pkg.camera_functions=lambda source,runtime:camera_component.functions(component,native)
  prepared=root/'prepared';(prepared/'web/locales').mkdir(parents=True)
  arm=bytearray(64);arm[:6]=b'\x7fELF\x01\x01';arm[18]=40
  for name in ('cc2-control','cc2-update-worker','cc2-https-download'):(prepared/name).write_bytes(arm)
  (prepared/'web/index.html').write_text('fixture UI');(prepared/'web/locales/en.json').write_text('{}')
  for name in ('ca-bundle.pem','update-runner.sh','updater-trusted-key.json','THIRD_PARTY_NOTICES.txt'):(prepared/name).write_text('fixture')
  source=root/'source.zip';source.write_bytes(b'fixture source')
  output=root/'installer.zip';pkg.package(prepared,ROOT/'scripts',source,output,camera_runtime=native)
  camera_component.PINS=saved_pins
  with zipfile.ZipFile(output) as archive:
   payload=next(archive.read(n) for n in archive.namelist() if n.endswith('/cc2-control-payload.tar.gz'))
  with tarfile.open(fileobj=io.BytesIO(payload),mode='r:gz') as archive:
   for member in archive:
    p=stage/member.name;p.parent.mkdir(parents=True,exist_ok=True);p.write_bytes(archive.extractfile(member).read());p.chmod(member.mode)
  marker=root/'running';init=root/'app-init'
  init.write_text('#!/bin/sh\ncase "$1" in start) touch "'+str(marker)+'";;stop) rm -f "'+str(marker)+'";;esac\n');init.chmod(0o755)
  (stage/'cc2-control.init').write_bytes(init.read_bytes())
  (stage/'SHA256SUMS').write_text(''.join(sha(p)+'  '+p.relative_to(stage).as_posix()+'\n' for p in sorted(stage.rglob('*')) if p.is_file() and p.name!='SHA256SUMS'))
  commands=root/'bin';commands.mkdir()
  vendor=root/'vendor';vendor.write_bytes(b'qualified-printer')
  daemon=root/'daemon';daemon.write_bytes(b'qualified-daemon')
  state='{"connected":true,"machine":{"status":1,"x":0},"last_message_age":0,"extruder":{"temperature":24,"target":0},"heater_bed":{"temperature":24,"target":0},"x":0}'
  health='{"service":"cc2-control","version":"'+pkg.VERSION+'","mqtt_registered":true,"snapshot_received":true}'
  (commands/'wget').write_text('#!/bin/sh\ncase "$*" in *api/printer*) echo \''+state+'\';;*) '+('exit 1' if mode=='health-fail' else 'echo \''+health+'\'')+';;esac\n')
  (commands/'pidof').write_text('#!/bin/sh\nif [ "$1" = elegoo_printer ];then echo 123;elif [ -f "'+str(marker)+'" ];then echo 456;else exit 1;fi\n')
  (commands/'sleep').write_text('#!/bin/sh\nexit 0\n')
  for p in commands.iterdir():p.chmod(0o755)
  def rewrite(text):
   for a,b in [('/opt/usr/cc2-control',str(target)),('/etc/init.d/cc2-control',str(init)),('/etc/init.d/printer',str(camera_init)),
               ('/etc/init.d/.cc2-camera-restore-',str(root/'.camera-restore-')),('/tmp/cc2-control-install.lock',str(root/'lock')),
               ('/opt/bin/elegoo_printer',str(vendor)),('/opt/inst/daemon-000/daemon-000',str(daemon)),
               ('/proc/$printer_pid/exe',str(vendor)),('/proc/$RUN_PID/exe',str(target/'cc2-control')),
               (pkg.COMPATIBILITY['printer_sha256'],sha(vendor)),(pkg.COMPATIBILITY['daemon_sha256'],sha(daemon)),
               ('bcabb1fa405b066b06c229c6d8cc6e474a0ccc57e31a1b5b387d3b55945be563',hashlib.sha256(original).hexdigest())]:text=text.replace(a,b)
   return text
  script=root/'run.sh';script.write_text(rewrite((stage/'install-on-printer.sh').read_text()).replace('STAGE=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)','STAGE="'+str(stage)+'"'))
  env=dict(os.environ,PATH=str(commands)+':'+os.environ['PATH'],CAMERA_CASE=mode,CAMERA_TARGET=str(target),CAMERA_INIT=str(camera_init))
  result=subprocess.run(['sh',str(script)],env=env,capture_output=True,text=True,timeout=20)
  assert (result.returncode==0)==(mode=='success'),(mode,result.stdout,result.stderr)
  if mode=='foreign-edit':
   assert camera_init.read_bytes().endswith(b'# unrelated change\n')
   assert (target/'cc2-control').read_bytes()==bytes(arm)
  elif mode=='success':
   assert 'REBOOT REQUIRED' in result.stdout and camera_init.read_bytes()!=original
   backup=next(target.parent.glob('cc2-control-backup-*'))
   restore=backup/'restore-fixture.sh';restore.write_text(rewrite((backup/'restore.sh').read_text()))
   restored=subprocess.run(['sh',str(restore)],env=env,capture_output=True,text=True,timeout=10)
   assert restored.returncode==0,(restored.stdout,restored.stderr)
   assert camera_init.read_bytes()==original and (target/'cc2-control').read_bytes()==b'old-app'
  else:
   assert camera_init.read_bytes()==original and (target/'cc2-control').read_bytes()==b'old-app',mode
  assert not (root/'lock').exists()
print('PASS: embedded archive, preflight refusal, healthy install, health failure, component failure, post-commit rollback, exact manual restore and foreign-edit refusal')
