from pathlib import Path
import hashlib,json,os,re,shlex,subprocess,tempfile
import sys
script=Path(sys.argv[1]).read_text()
shell=shlex.split(os.environ.get('CC2_TEST_SHELL','sh'))
IDLE='{"connected":true,"last_message_age":2,"extruder":{"temperature":25.0,"target":0.0},"heater_bed":{"temperature":25.0,"target":0.0},"machine":{"status":1,"status_name":"Idle"}}'
def put(p,b):
    p.parent.mkdir(parents=True,exist_ok=True)
    p.write_bytes(b if isinstance(b,bytes) else b.encode())
class Fixture:
    def __init__(self,parent):
        self.root=Path(tempfile.mkdtemp(dir=parent));self.usr=self.root/'opt/usr';self.app=self.usr/'cc2-control'
        self.before={}
        self.keep=['cc2-control/cc2-control','cc2-control/camera/libcc2-camera-reap-r2.so','cc2-control/material-presets.json','cc2-control/bed-plates.json','cc2-control/spools.json','cc2-reactor-runtime-v2/elegoo_printer','cfg/autosave.cfg','cfg/device_model.cfg','cfg/password_file','gcode/example.gcode','cc2-control-backup-unknown/keep']
        for n in self.keep:put(self.usr/n,'KEEP '+n);self.before[n]=(self.usr/n).read_bytes()
        for n in ('cc2-control.conf','cc2-control.conf.new','ui-preferences.json','ui-preferences.json.tmp','ui-preferences.json.new'):put(self.app/n,'PRIVATE-CONFIG-TOKEN')
        self.remove=['cc2-control-backup-20261024-061327-2430','cc2-reactor-before-firmware-1976','cc2-camera-recovery-r2','cc2-camera-reap-boot-r1','cc2-camera-before-reinstall-20261023','cc2-control-updates']
        for n in self.remove:put(self.usr/n/'old.txt','discard')
        for n in ('cc2-control.init-pre-1.1.24','cc2-control.inst-launch-pre-1.1.24'):put(self.usr/n,'old file')
        put(self.root/'etc/init.d/printer','canonical camera and reactor startup\n')
        put(self.root/'proc/mounts','device '+str(self.usr)+' ext4 rw 0 0\n')
        (self.root/'tmp').mkdir();self.stub=self.root/'stub';self.stub.mkdir()
        self.log=self.root/'service.log';self.running=self.root/'running';self.running.touch()
        put(self.root/'etc/init.d/cc2-control',f'#!/bin/sh\necho "$1" >> "{self.log}"\ncase "$1" in stop) [ "${{TEST_STOP_FAIL:-0}}" = 0 ] || exit 1;rm -f "{self.running}";; start) touch "{self.running}";; esac\n')
        put(self.stub/'id','#!/bin/sh\necho 0\n')
        put(self.stub/'sync','#!/bin/sh\nexit 0\n')
        put(self.stub/'wget','#!/bin/sh\ncase "$*" in *app-update*) printf \'%s\' "$TEST_UPDATE";; *) printf \'%s\' "$TEST_STATE";; esac\n')
        put(self.stub/'pidof',f'#!/bin/sh\ncase "$1" in cc2-update-worker) [ "${{TEST_WORKER:-0}}" = 1 ] || exit 1;echo 99;; cc2-control) [ -f "{self.running}" ] || exit 1;echo 123;; *) exit 1;; esac\n')
        for p in [self.root/'etc/init.d/cc2-control',*self.stub.iterdir()]:p.chmod(0o700)
        self.script=self.root/'cleanup.sh'
        put(self.script,re.sub(r'/(?:opt|etc|proc|tmp)/',lambda m:str(self.root)+m[0],script))
        self.env=dict(os.environ,PATH=str(self.stub)+':'+os.environ['PATH'],TEST_STATE=IDLE,TEST_UPDATE='{"busy":false}')
    def run(self,mode,ok=True,**env):
        r=subprocess.run([*shell,str(self.script),mode],env=dict(self.env,**env),capture_output=True,text=True,timeout=25)
        assert (r.returncode==0)==ok,(mode,r.stdout,r.stderr)
        assert 'PRIVATE-CONFIG-TOKEN' not in r.stdout+r.stderr
        for n,b in self.before.items():assert (self.usr/n).read_bytes()==b,n
        return r
count=0
with tempfile.TemporaryDirectory(prefix='cc2-cleanup-test-') as temp:
    f=Fixture(temp);f.run('check');assert not f.log.exists() and (f.app/'cc2-control.conf').exists()
    f.run('apply');assert f.log.read_text().splitlines()==['stop'] and not f.running.exists()
    for n in f.remove:assert not (f.usr/n).exists(),n
    assert not (f.app/'cc2-control.conf').exists() and not (f.app/'ui-preferences.json').exists();count+=2
    for env in [dict(TEST_STATE=IDLE.replace('age":2','age":99')),dict(TEST_STATE=IDLE.replace('"status":1','"status":2')),dict(TEST_STATE=IDLE.replace('25.0','80.0')),dict(TEST_STATE=IDLE.replace('"target":0.0','"target":60.0')),dict(TEST_STATE='{}'),dict(TEST_UPDATE='{"busy":true}'),dict(TEST_WORKER='1')]:
        f=Fixture(temp);f.run('apply',ok=False,**env);assert not f.log.exists() and (f.app/'cc2-control.conf').exists();count+=1
    f=Fixture(temp);(f.usr/'cc2-camera-before-firmware-99').symlink_to(f.usr/'cfg',target_is_directory=True)
    f.run('apply',ok=False);assert not f.log.exists() and (f.usr/f.remove[0]).exists();count+=1
    for mode in ('mapped','exe','cwd','cmdline','mount','rollback','startup'):
        f=Fixture(temp);p=f.usr/'cc2-camera-recovery-r2'
        if mode=='mapped':put(f.root/'proc/123/maps',str(p)+'/old.so\n')
        elif mode in ('exe','cwd'):
            d=f.root/'proc/123';d.mkdir(parents=True);(d/mode).symlink_to(p if mode=='cwd' else p/'old.txt')
        elif mode=='cmdline':put(f.root/'proc/123/cmdline',str(p/'start.sh').encode()+b'\0')
        elif mode=='mount':put(f.root/'proc/mounts','device '+str(p/'nested')+' ext4 rw 0 0\n')
        elif mode=='rollback':put(f.app/'camera/printer.original',str(p/'boot-camera.sh'))
        elif mode=='startup':put(f.root/'etc/init.d/printer',str(p/'boot-camera.sh'))
        r=f.run('apply');assert p.exists() and 'KEEP:' in r.stdout;count+=1
    f=Fixture(temp);f.run('apply',ok=False,TEST_STOP_FAIL='1');assert f.log.read_text().splitlines()==['stop'] and (f.app/'cc2-control.conf').exists();count+=1
print('PASS',count,'cleanup cases, including exact scope, preservation, dry run, refusal, active/mounted/rollback retention and no restart; shell='+str(shell))
print('TESTED_SCRIPT_SHA256='+hashlib.sha256(script.encode()).hexdigest())
