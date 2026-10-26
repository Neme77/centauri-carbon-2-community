"""Shell guard fixtures; never address a real process, printer or /proc path."""
import hashlib,os,subprocess,tempfile
from pathlib import Path
HERE=Path(__file__).resolve().parent
assert 'CC2_CAMERA_REAP_ENABLE_LAN=1' in (HERE/'trial.sh').read_text()
assert 'CC2_CAMERA_REAP_ENABLE_LAN=0' not in (HERE/'trial.sh').read_text()
IDLE='{"connected":true,"last_message_age":2,"machine":{"status":1,"status_name":"Idle"}}'
def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()
with tempfile.TemporaryDirectory() as directory:
    root=Path(directory);base=root/'package';base.mkdir();stubs=root/'stubs';stubs.mkdir()
    proc=root/'proc/111';proc.mkdir(parents=True);camera=root/'ai_camera';camera.write_text('fixture-camera')
    (proc/'exe').symlink_to(camera);(proc/'maps').write_text('fixture maps')
    loader=root/'loader';loader.write_text('#!/bin/sh\nexit 0\n');loader.chmod(0o700)
    for name in ('libcc2-camera-reap-r2.so','libcc2-camera-recovery-r2.so','cc2-camera-reap-check'):
        p=base/name;p.write_text('#!/bin/sh\nexit 0\n');p.chmod(0o700)
    source=(HERE/'trial.sh').read_text().replace('/tmp/cc2-camera-recovery-r2',str(base)).replace('/proc/$PID',str(root)+'/proc/$PID')
    source=source.replace('CAMERA=/opt/bin/ai_camera',f'CAMERA={camera}').replace('/lib/ld-linux-armhf.so.3',str(loader))
    for var,digest in [('QUALIFIED',sha(camera)),('REAP_SHA',sha(base/'libcc2-camera-reap-r2.so')),('HELPER_SHA',sha(base/'cc2-camera-reap-check'))]:
        import re
        source=re.sub(r'^'+var+r'=[a-f0-9]+$',var+'='+digest,source,flags=re.M)
    (base/'trial.sh').write_text(source)
    for name,body in {'id':'echo 0','pidof':'echo "${FIXTURE_PID-111}"','wget':'printf "%s" "$FIXTURE_STATE"',
                      'kill':'echo UNEXPECTED_KILL >&2; exit 99','reboot':'echo UNEXPECTED_REBOOT >&2; exit 99'}.items():
        p=stubs/name;p.write_text('#!/bin/sh\n'+body+'\n');p.chmod(0o700)
    def manifest():
        names=['trial.sh','libcc2-camera-reap-r2.so','libcc2-camera-recovery-r2.so','cc2-camera-reap-check']
        (base/'SHA256SUMS').write_text(''.join(sha(base/n)+'  '+n+'\n' for n in names))
    def call(state=IDLE,pid='111',ok=False,command='check'):
        env=dict(os.environ,PATH=str(stubs)+':'+os.environ['PATH'],FIXTURE_STATE=state,FIXTURE_PID=pid)
        r=subprocess.run(['sh',str(base/'trial.sh'),command],capture_output=True,text=True,env=env,timeout=10)
        assert (r.returncode==0)==ok,(state,pid,r.stdout,r.stderr)
        assert 'UNEXPECTED_' not in r.stdout+r.stderr
        return r
    manifest();call(ok=True)
    (proc/'maps').write_text('/tmp/cc2-camera-recovery-r1/libcc2-camera-recovery-r1.so')
    call(command='start');(proc/'maps').write_text('fixture maps')
    for state in [IDLE.replace('true','false'),IDLE.replace('"status":1','"status":2'),IDLE.replace('"status":1','"status":3'),IDLE.replace('age":2','age":99'),IDLE.replace('"last_message_age":2,',''),'{}']:
        call(state=state,command='start')
    for pid in ['','111 222','x']:call(pid=pid,command='start')
    camera.write_text('wrong-camera');call(command='start');camera.write_text('fixture-camera')
    helper=base/'cc2-camera-reap-check';helper.write_text('tampered');call(command='start')
    helper.write_text('#!/bin/sh\nexit 0\n');manifest();call(ok=True)
    print('PASS: 14 shell preflight cases and forced-LAN invariant; no process signalled or system file modified')
