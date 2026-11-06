"""Real bounded publisher and installer wiring, inert app/native services.

The configured production worker has a separate actual-process qualification.
Here an ARM worker double records ordering/failures at the installer boundary;
no simulated transfer is presented as physical hardware validation.
"""
from pathlib import Path
import hashlib, importlib.util, io, json, os, shutil, subprocess, sys, tarfile, tempfile, zipfile
from publish_bundle import configuration, runtime
from test_installer_rfid import put

HERE=Path(__file__).resolve().parent
APP=HERE.parents[2]/'cc2-control'
sys.path.insert(0,str(APP/'installer'))
from app_binding_component import functions
spec=importlib.util.spec_from_file_location('binding_package',APP/'installer/package.py')
pkg=importlib.util.module_from_spec(spec);spec.loader.exec_module(pkg)
sha=lambda b:hashlib.sha256(b).hexdigest()
NAMES=('coordinator','capture','app-integration.json','activate-on-ready')


def run():
    count=0
    with tempfile.TemporaryDirectory(prefix='cc2-binding-delivery-') as temporary:
        root=Path(temporary);fixture=root/'fixture';updates=fixture/'updates'
        prepared=root/'prepared';source=root/'source.zip';put(source,b'fixture source')
        binary=bytearray(64);binary[:6]=b'\x7fELF\x01\x01';binary[18]=40;binary=bytes(binary)
        app_sha=sha(binary);destination=updates/app_sha
        for name in ['cc2-control','cc2-update-worker','cc2-https-download']:put(prepared/name,binary)
        for name,data in [('ca-bundle.pem',b'CA'),('THIRD_PARTY_NOTICES.txt',b'notices'),
                          ('updater-trusted-key.json',b'{}'),('web/index.html',b'UI'),('web/locales/en.json',b'{}')]:put(prepared/name,data)
        put(prepared/'update-runner.sh',(APP/'scripts/update-runner.sh').read_bytes())
        worker_source=root/'double.c';worker=root/'worker'
        # The double verifies the held directory descriptor and service ordering.
        worker_source.write_text('''#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
int main(int argc,char **argv){
    struct stat st;if(argc<3||fstat(3,&st)||!S_ISDIR(st.st_mode)||(st.st_mode&07777)!=0700)return 90;
    const char *trace=getenv("TRACE"),*marker=getenv("MARKER"),*fail=getenv("BINDING_FAIL");
    if(!trace||!marker)return 91;
    FILE *f=fopen(trace,"a");if(!f)return 92;
    fprintf(f,"%s\\n",argv[1]);fclose(f);
    if(!strcmp(argv[1],"--prepare"))return access(marker,F_OK)?93:0;
    if(!strcmp(argv[1],"--commit"))return access(marker,F_OK)?94:(fail&&!strcmp(fail,"commit"));
    if(!strcmp(argv[1],"--rollback"))return !access(marker,F_OK)?95:(fail&&!strcmp(fail,"rollback"));
    return 96;
}
''')
        subprocess.run(['arm-linux-gnueabihf-gcc','-O2','-Wall','-Wextra','-Werror','-static',worker_source,'-o',worker],check=True,timeout=120)
        files={'worker':worker.read_bytes(),'recovery.json':json.dumps({'protocol':1,'app_sha256':app_sha,
            'worker_sha256':sha(worker.read_bytes()),'plan_sha256':'ab'*32}).encode()}
        files.update({'incoming/'+n:('new '+n).encode() for n in NAMES})
        modes={n:0o700 if n in ['worker','incoming/coordinator','incoming/capture'] else 0o600 for n in files}
        dependency=fixture/'native-proof';config=root/'publisher.h'
        config.write_bytes(configuration(files,modes,[(str(dependency),sha(b'native unchanged'),0o600)],app_binding=True))
        publisher=root/'publisher';defines={'CP_PARENT':str(updates),'CP_LOCK':str(fixture/'lock'),
            'CP_TARGET':app_sha,'CP_CONFIG':str(config)}
        subprocess.run(['arm-linux-gnueabihf-gcc','-O2','-std=c11','-Wall','-Wextra','-Wpedantic','-Werror','-static',
            '-DCP_APP_BINDING=1',*['-D'+k+'='+json.dumps(v) for k,v in defines.items()],HERE/'publish_worker.c','-o',publisher],check=True,timeout=120)
        bundle=root/'binding.tar.gz';bundle.write_bytes(runtime(files,modes,publisher.read_bytes()))
        functions(bundle,app_sha)
        try:functions(bundle,'ef'*32)
        except ValueError:count+=1
        else:raise AssertionError('wrong app admitted')
        output=root/'update.zip';pkg.package(prepared,APP/'scripts',source,output,app_binding_runtime=bundle)
        with zipfile.ZipFile(output) as archive:
            payload=archive.read(next(n for n in archive.namelist() if n.endswith('/cc2-control-payload.tar.gz')))
        for shell in [['dash'],['busybox','sh']]:
            for case in ['success','commit-failure','health-failure','rollback-failure','tampered-tail','foreign-destination','native-changed']:
                if fixture.exists():shutil.rmtree(fixture)
                fixture.mkdir(mode=0o700);stage=fixture/'stage';stage.mkdir(mode=0o700)
                with tarfile.open(fileobj=io.BytesIO(payload),mode='r:gz') as archive:
                    for entry in archive:put(stage/entry.name,archive.extractfile(entry).read(),entry.mode)
                target=fixture/'app';put(target/'cc2-control',b'OLD APP');put(target/'spools.json',b'PRESERVE')
                marker=fixture/'running';marker.touch();trace=fixture/'trace'
                init=fixture/'init';put(init,f'#!/bin/sh\ncase "$1" in start) touch "{marker}";; stop) rm -f "{marker}";; esac\n',0o755)
                put(stage/'cc2-control.init',init.read_bytes(),0o755)
                printer=fixture/'printer';daemon=fixture/'daemon';put(printer,b'printer data');put(daemon,b'daemon data')
                put(dependency,b'altered' if case=='native-changed' else b'native unchanged')
                put(fixture/'native-journal',b'NATIVE JOURNAL PRESERVED')
                if case=='foreign-destination':put(destination/'foreign',b'KEEP EVIDENCE')
                commands=fixture/'bin';commands.mkdir()
                health=json.dumps({'service':'cc2-control','version':pkg.VERSION if case!='health-failure' else 'wrong',
                    'mqtt_registered':True,'snapshot_received':True},separators=(',',':'))
                status='{"connected":true,"last_message_age":1,"extruder":{"temperature":24,"target":0},"heater_bed":{"temperature":22,"target":0},"machine":{"status":1,"sub_status":0}}'
                put(commands/'wget',f'#!/bin/sh\ncase "$*" in */api/health*) echo \'{health}\';; *) echo \'{status}\';; esac\n',0o755)
                put(commands/'pidof',f'#!/bin/sh\ncase "$1" in elegoo_printer) echo 200;; cc2-control) [ -f "{marker}" ] && echo 100;; esac\n',0o755)
                put(commands/'sleep','#!/bin/sh\n[ "$1" != 2 ] || exit 0\nexec /bin/sleep "$@"\n',0o755)
                memory=fixture/'meminfo';put(memory,b'MemAvailable: 65000 kB\n')
                replacements={'/opt/usr/cc2-control':str(target),'/etc/init.d/cc2-control':str(init),
                    '/tmp/cc2-control-install.lock':str(fixture/'lock'),'/opt/usr/cc2-rfid-app-updates':str(updates),
                    '/opt/bin/elegoo_printer':str(printer),'/opt/inst/daemon-000/daemon-000':str(daemon),
                    '/proc/$printer_pid/exe':str(printer),'/proc/$RUN_PID/exe':str(target/'cc2-control'),
                    '/proc/meminfo':str(memory),pkg.COMPATIBILITY['printer_sha256']:sha(printer.read_bytes()),
                    pkg.COMPATIBILITY['daemon_sha256']:sha(daemon.read_bytes())}
                script=(stage/'install-on-printer.sh').read_text()
                for before,after in replacements.items():script=script.replace(before,after)
                # Adapt executable calls only. All hash checks still inspect ARM ELFs.
                script=script.replace('"$BINDING_SOURCE/publisher" --publish','qemu-arm "$BINDING_SOURCE/publisher" --publish')
                for action in ['--prepare','--commit','--rollback']:
                    script=script.replace(str(destination)+'/worker '+action,'qemu-arm '+str(destination)+'/worker '+action)
                script=script.replace('"$tries" -lt 120','"$tries" -lt 2')
                if case=='tampered-tail':script=script.replace("printf '\\037", "printf '\\000",1)
                put(stage/'install-on-printer.sh',script,0o755)
                (stage/'SHA256SUMS').write_text(''.join(sha(p.read_bytes())+'  '+p.relative_to(stage).as_posix()+'\n'
                    for p in sorted(stage.rglob('*')) if p.is_file() and p.name!='SHA256SUMS'))
                env=dict(os.environ,PATH=str(commands)+':'+os.environ['PATH'],TRACE=str(trace),MARKER=str(marker),
                    BINDING_FAIL='rollback' if case=='rollback-failure' else 'commit' if case=='commit-failure' else '')
                if case=='rollback-failure':
                    health=health.replace(pkg.VERSION,'wrong')
                    put(commands/'wget',f'#!/bin/sh\ncase "$*" in */api/health*) echo \'{health}\';; *) echo \'{status}\';; esac\n',0o755)
                result=subprocess.run([*shell,stage/'install-on-printer.sh'],env=env,capture_output=True,text=True,timeout=90)
                assert (result.returncode==0)==(case=='success'),(shell,case,result.stdout,result.stderr)
                calls=trace.read_text().splitlines() if trace.exists() else []
                if case=='success':assert calls==['--prepare','--commit'] and (target/'cc2-control').read_bytes()==binary
                if case in ['commit-failure','health-failure']:
                    assert calls[-1]=='--rollback' and (target/'cc2-control').read_bytes()==b'OLD APP' and marker.exists(),(shell,case,calls)
                if case=='rollback-failure':
                    assert calls[-1]=='--rollback' and (target/'cc2-control').read_bytes()==binary and not marker.exists()
                    assert (fixture/'lock').exists(),'uncertain rollback must retain barrier'
                else:assert not (fixture/'lock').exists()
                if case in ['tampered-tail','foreign-destination','native-changed']:assert not calls and (target/'cc2-control').read_bytes()==b'OLD APP'
                assert (target/'spools.json').read_bytes()==b'PRESERVE'
                assert (fixture/'native-journal').read_bytes()==b'NATIVE JOURNAL PRESERVED'
                if case=='success':
                    backup=next(fixture.glob('app-backup-*'))
                    restore=(backup/'restore.sh').read_text()
                    for before,after in replacements.items():restore=restore.replace(before,after)
                    restore=restore.replace(str(destination)+'/worker --rollback','qemu-arm '+str(destination)+'/worker --rollback')
                    put(backup/'restore.sh',restore,0o755)
                    restored=subprocess.run([*shell,backup/'restore.sh'],env=env,capture_output=True,text=True,timeout=30)
                    assert restored.returncode==0,(shell,'backup restore',restored.stdout,restored.stderr)
                    assert (target/'cc2-control').read_bytes()==b'OLD APP' and marker.exists()
                    assert trace.read_text().splitlines()==['--prepare','--commit','--rollback']
                    assert not (fixture/'lock').exists()
                count+=1
    print(f'PASS: {count} real ARM publication and dash/BusyBox installer boundary cases; prepare/commit/rollback ordering, failure retention and preserved native journal; worker double at installer boundary only',flush=True)
    return count

if __name__=='__main__':run()
