"""Real package/installer/publisher on dash and BusyBox, inert app/service data.

Only publisher (our code) runs on ARM. App/vendor ELF bytes, service and network
are fake; no native executable, serial device or firmware transfer is executed.
"""
from pathlib import Path
import hashlib
import importlib.util
import io
import json
import os
import shutil
import subprocess
import sys
import tarfile
import tempfile
import zipfile
from publish_bundle import configuration,runtime
from test_stage_worker import PATHS

HERE=Path(__file__).resolve().parent
APP=HERE.parents[2]/'cc2-control'
sys.path.insert(0,str(APP/'installer'))
spec=importlib.util.spec_from_file_location('rfid_installer_package',APP/'installer/package.py')
pkg=importlib.util.module_from_spec(spec);spec.loader.exec_module(pkg)
sha=lambda data:hashlib.sha256(data).hexdigest()
CASES=['success','private-umask','dependency-changed','interrupted-copy','health-failure','active-lifecycle',
       'tampered-tail','busy','existing-public-lock','rollback-live','ordinary-replacement']

def put(path,data,mode=0o600):
    path.parent.mkdir(parents=True,exist_ok=True,mode=0o700)
    path.write_bytes(data if isinstance(data,bytes) else data.encode());path.chmod(mode)

def emulate_publisher(script):
    # Adapt only the two executable invocations. The publisher path in the
    # checksum and chmod commands must continue to name the real ARM file.
    for action in ['--check','--publish']:
        command='    "$RFID_SOURCE/publisher" '+action+' "$RFID_SOURCE/component"\n'
        assert script.count(command)==1,('publisher invocation changed',action)
        script=script.replace(command,'    qemu-arm '+command.lstrip(),1)
    assert 'sha256sum "$RFID_SOURCE/publisher"' in script
    assert 'chmod 700 "$RFID_SOURCE/publisher"' in script
    assert 'sha256sum qemu-arm' not in script and 'chmod 700 qemu-arm' not in script
    return script

def run():
    count=0
    with tempfile.TemporaryDirectory(prefix='cc2-rfid-installer-') as temporary:
        root=Path(temporary);fixture=root/'fixture';source=root/'source.zip';source.write_bytes(b'source fixture')
        prepared=root/'prepared';(prepared/'web/locales').mkdir(parents=True)
        binary=bytearray(64);binary[:6]=b'\x7fELF\x01\x01';binary[18]=40;binary=bytes(binary)
        for name in ['cc2-control','cc2-update-worker','cc2-https-download']:put(prepared/name,binary)
        for name,content in [('ca-bundle.pem',b'fixture CA'),('THIRD_PARTY_NOTICES.txt',b'fixture notices'),
                             ('updater-trusted-key.json',b'{}'),('web/index.html',b'fixture UI'),('web/locales/en.json',b'{}')]:put(prepared/name,content)
        put(prepared/'update-runner.sh',(APP/'scripts/update-runner.sh').read_bytes())
        profile=b'fixture recovery profile\n'
        metadata={'protocol':1,'app_sha256':sha(binary),'coordinator_sha256':sha(binary),
                  'capture_sha256':sha(binary),'profile_sha256':sha(profile)}
        files={'app-integration.json':json.dumps(metadata).encode(),'recovery-profile':profile,
            'coordinator':binary,'capture':binary,'payload/0':b'future startup bytes',
            'activate-on-ready':f'CC2-RFID-ACTIVATE-1\n{sha(binary)}\n{sha(profile)}\n'.encode()}
        modes={n:(0o700 if n in ['coordinator','capture'] else 0o600) for n in files}
        before=[(path,sha(f'original startup {i}'.encode()),0o755) for i,path in enumerate(PATHS)]
        config=root/'publisher.h';config.write_bytes(configuration(files,modes,before))
        fault=root/'fault.h';fault.write_text('#define _GNU_SOURCE\n#include <stdlib.h>\n#include <string.h>\n#include <unistd.h>\nstatic void cp_fault(const char*p){const char*c=getenv("CP_CRASH");if(c&&!strcmp(c,p))_exit(91);}\n#define CP_FAULT(p) cp_fault(p)\n')
        publisher=root/'publisher'
        defines={'CP_PARENT':str(fixture/'opt/usr'),'CP_PREFIX':str(fixture/'tree'),
                 'CP_LOCK':str(fixture/'lock'),'CP_CONFIG':str(config)}
        subprocess.run(['arm-linux-gnueabihf-gcc','-O2','-std=c11','-Wall','-Wextra','-Wpedantic','-Werror','-static',
            *['-D'+k+'='+json.dumps(v) for k,v in defines.items()],'-include',fault,HERE/'publish_worker.c','-o',publisher],check=True,timeout=120)
        native=root/'runtime.tar.gz';native.write_bytes(runtime(files,modes,publisher.read_bytes()))
        packages={}
        for variant,native_arg in [('native',native),('ordinary',None)]:
            output=root/(variant+'.zip');pkg.package(prepared,APP/'scripts',source,output,rfid_runtime=native_arg)
            with zipfile.ZipFile(output) as archive:
                payload=next(n for n in archive.namelist() if n.endswith('/cc2-control-payload.tar.gz'))
                packages[variant]=archive.read(payload)
        for shell in [['dash'],['busybox','sh']]:
            for case in CASES:
                if fixture.exists():shutil.rmtree(fixture)
                fixture.mkdir(mode=0o700);stage=fixture/'stage';stage.mkdir(mode=0o700)
                with tarfile.open(fileobj=io.BytesIO(packages['ordinary' if case=='ordinary-replacement' else 'native']),mode='r:gz') as archive:
                    for entry in archive:
                        put(stage/entry.name,archive.extractfile(entry).read(),entry.mode)
                target=fixture/'opt/usr/cc2-control';put(target/'cc2-control',b'PREVIOUS APP')
                put(target/'preferences.json',b'KEEP');marker=fixture/'app-running';marker.touch()
                init=fixture/'init';put(init,f'#!/bin/sh\ncase "$1" in start) touch "{marker}";; stop) rm -f "{marker}";; esac\n',0o755)
                put(stage/'cc2-control.init',init.read_bytes(),0o755)
                printer=fixture/'printer';daemon=fixture/'daemon';put(printer,b'inert printer data');put(daemon,b'inert daemon data')
                originals={}
                for i,path in enumerate(PATHS):
                    data=f'original startup {i}'.encode();originals[path]=data;put(fixture/'tree'/path[1:],data,0o755)
                if case=='dependency-changed':put(fixture/'tree'/PATHS[0][1:],b'foreign edit',0o755)
                component=fixture/'opt/usr/cc2-rfid'
                if case in ['active-lifecycle','ordinary-replacement']:put(component/'lifecycle',b'active evidence')
                if case=='busy':(fixture/'lock').mkdir(mode=0o700)
                if case=='existing-public-lock':(fixture/'lock').mkdir(mode=0o755)
                commands=fixture/'bin';commands.mkdir()
                health='{"service":"cc2-control","version":"'+pkg.VERSION+'","mqtt_registered":true,"snapshot_received":true}'
                if case=='health-failure':health=health.replace(pkg.VERSION,'wrong')
                status='{"connected":true,"last_message_age":1,"extruder":{"temperature":24,"target":0},"heater_bed":{"temperature":22,"target":0},"machine":{"status":1,"sub_status":0}}'
                put(commands/'wget',f'#!/bin/sh\ncase "$*" in */api/health*) printf \'%s\\n\' \'{health}\';; *) printf \'%s\\n\' \'{status}\';; esac\n',0o755)
                put(commands/'pidof',f'#!/bin/sh\ncase "$1" in elegoo_printer) echo 200;; cc2-control) [ -f "{marker}" ] && echo 100;; esac\n',0o755)
                put(commands/'sleep','#!/bin/sh\n[ "$1" != 2 ] || exit 0\nexec /bin/sleep "$@"\n',0o755)
                # Observe the actual lock made by the real installer, before
                # publisher preflight. Never repair its permissions in fixtures.
                lock_mode=fixture/'observed-lock-mode'
                put(commands/'qemu-arm',f'#!/bin/sh\nls -ld "{fixture}/lock" | awk \'{{print $1}}\' >> "{lock_mode}"\nexec "{shutil.which("qemu-arm")}" "$@"\n',0o755)
                memory=fixture/'meminfo';put(memory,b'MemAvailable: 65000 kB\n')
                script=(stage/'install-on-printer.sh').read_text()
                replacements={'/opt/usr/cc2-control':str(target),'/etc/init.d/cc2-control':str(init),
                    '/tmp/cc2-control-install.lock':str(fixture/'lock'),'/opt/bin/elegoo_printer':str(printer),
                    '/opt/inst/daemon-000/daemon-000':str(daemon),'/proc/$printer_pid/exe':str(printer),
                    '/proc/$RUN_PID/exe':str(target/'cc2-control'),'/proc/meminfo':str(memory),
                    '/opt/usr/cc2-rfid':str(component),pkg.COMPATIBILITY['printer_sha256']:sha(printer.read_bytes()),
                    pkg.COMPATIBILITY['daemon_sha256']:sha(daemon.read_bytes()),
                    '"$tries" -lt 120':'"$tries" -lt 3'}
                for old,new in replacements.items():script=script.replace(old,new)
                if case!='ordinary-replacement':script=emulate_publisher(script)
                if case=='tampered-tail':
                    at=script.index('# CC2_RFID_DATA_BEGIN\n')+len('# CC2_RFID_DATA_BEGIN\n')
                    changed=script[at:].replace('\\037','\\036',1);assert changed!=script[at:];script=script[:at]+changed
                put(stage/'install-on-printer.sh',script,0o755)
                restore=(stage/'restore-on-printer.sh').read_text()
                for old,new in replacements.items():restore=restore.replace(old,new)
                put(stage/'restore-on-printer.sh',restore,0o755)
                put(stage/'SHA256SUMS',''.join(sha(p.read_bytes())+'  '+p.relative_to(stage).as_posix()+'\n' for p in sorted(stage.rglob('*')) if p.is_file() and p.name!='SHA256SUMS'))
                env=dict(os.environ,PATH=str(commands)+':'+os.environ['PATH'])
                if case=='interrupted-copy':env['CP_CRASH']='copied'
                result=subprocess.run([*shell,stage/'install-on-printer.sh'],env=env,capture_output=True,text=True,timeout=40,
                    umask=0o077 if case=='private-umask' else 0o022)
                success=case in ['success','private-umask','rollback-live']
                if (result.returncode==0)!=success and Path('/output').is_dir():
                    # Retain complete diagnostics from this entirely fake tree
                    # before TemporaryDirectory removes it on a failed case.
                    observed={}
                    for path in sorted(fixture.rglob('*')):
                        st=path.lstat()
                        observed[path.relative_to(fixture).as_posix()]={
                            'mode':oct(st.st_mode&0o7777),'uid':st.st_uid,'gid':st.st_gid,
                            'size':st.st_size,'sha256':sha(path.read_bytes()) if path.is_file() else None}
                    Path('/output/installer-fixture-failure.json').write_text(json.dumps({
                        'shell':shell,'case':case,'exit':result.returncode,
                        'stdout':result.stdout,'stderr':result.stderr,'fake_tree':observed,
                        'printer_accessed':False},indent=2)+'\n')
                assert (result.returncode==0)==success,(shell,case,result.stdout[-2000:],result.stderr[-2000:])
                if case in ['success','private-umask']:
                    assert lock_mode.read_text().splitlines()==['drwx------','drwx------'],(shell,case,'installer lock must be private at check and publish')
                if case=='interrupted-copy':
                    assert not component.exists() and (target/'cc2-control').read_bytes()==b'PREVIOUS APP'
                    assert list((fixture/'opt/usr').glob('.cc2-rfid-stage-*'))
                    env.pop('CP_CRASH');retry=subprocess.run([*shell,stage/'install-on-printer.sh'],env=env,capture_output=True,text=True,timeout=40)
                    assert retry.returncode==0,(shell,case,retry.stdout[-1000:],retry.stderr[-1000:]);success=True
                if success:
                    assert (target/'cc2-control').read_bytes()==binary
                    for name,data in files.items():assert (component/name).read_bytes()==data
                    assert not (component/'lifecycle').exists()
                else:assert (target/'cc2-control').read_bytes()==b'PREVIOUS APP'
                assert (target/'preferences.json').read_bytes()==b'KEEP' and marker.exists()
                for path,data in originals.items():
                    expected=b'foreign edit' if case=='dependency-changed' and path==PATHS[0] else data
                    assert (fixture/'tree'/path[1:]).read_bytes()==expected
                if case=='rollback-live':
                    put(component/'lifecycle',b'active evidence')
                    backups=list((fixture/'opt/usr').glob('cc2-control-backup-*'));assert len(backups)==1
                    refused=subprocess.run([*shell,backups[0]/'restore.sh'],env=env,capture_output=True,text=True,timeout=15)
                    assert refused.returncode and (target/'cc2-control').read_bytes()==binary and marker.exists()
                if case=='existing-public-lock':assert (fixture/'lock').stat().st_mode&0o777==0o755
                elif case!='busy':assert not (fixture/'lock').exists()
                count+=1
    print(f'PASS: {count} actual RFID installer dash/BusyBox cases; dormant publication, request after unlock, unchanged startup, partial retry, health rollback and active-lifecycle refusal. No native/serial/firmware execution.')
    return count

if __name__=='__main__':run()
