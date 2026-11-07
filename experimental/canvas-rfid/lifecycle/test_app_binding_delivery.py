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


def run(unified=False):
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
    if(!strcmp(argv[1],"--check-current"))return access(marker,F_OK)?97:0;
    if(!strcmp(argv[1],"--commit"))return access(marker,F_OK)?94:(fail&&!strcmp(fail,"commit"));
    if(!strcmp(argv[1],"--rollback"))return !access(marker,F_OK)?95:(fail&&!strcmp(fail,"rollback"));
    return 96;
}
''')
        subprocess.run(['arm-linux-gnueabihf-gcc','-O2','-Wall','-Wextra','-Werror','-static',worker_source,'-o',worker],check=True,timeout=120)
        files={'worker':worker.read_bytes(),'recovery.json':json.dumps({'protocol':1,'app_sha256':app_sha,
            'worker_sha256':sha(worker.read_bytes()),'plan_sha256':'ab'*32}).encode()}
        files.update({'incoming/'+n:('new '+n).encode() for n in NAMES})
        if unified:
            profile=b'inert recovery profile'
            integration={'protocol':1,'app_sha256':app_sha,'coordinator_sha256':sha(binary),
                'capture_sha256':sha(binary),'profile_sha256':sha(profile)}
            fresh_files={'coordinator':binary,'capture':binary,'recovery-profile':profile,
                'app-integration.json':json.dumps(integration).encode(),
                'activate-on-ready':f'CC2-RFID-ACTIVATE-1\n{app_sha}\n{sha(profile)}\n'.encode()}
            for name in NAMES:files['incoming/'+name]=fresh_files[name]
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
        if unified:
            # Real namespace publishers and descriptor guard; native service,
            # floor manager actions and the app-bound worker are boundary doubles.
            # The production floor manager/worker run in separate suites.
            prefix=fixture/'opt/usr';floor_root=prefix/'cc2-cavity-floor'
            def publisher_for(label,items,permissions,target):
                header=root/(label+'.h');header.write_bytes(configuration(items,permissions,[(str(dependency),sha(b'native unchanged'),0o600)]))
                result=root/label
                defs={'CP_PARENT':str(prefix),'CP_TARGET':target,'CP_LOCK':str(fixture/'lock'),'CP_CONFIG':str(header)}
                subprocess.run(['arm-linux-gnueabihf-gcc','-O2','-std=c11','-Wall','-Wextra','-Wpedantic','-Werror','-static',
                    *['-D'+k+'='+json.dumps(v) for k,v in defs.items()],HERE/'publish_worker.c','-o',result],check=True,timeout=120)
                return runtime(items,permissions,result.read_bytes())
            rfid_modes={n:0o700 if n in ['coordinator','capture'] else 0o600 for n in fresh_files}
            rfid_runtime=publisher_for('rfid-publisher',fresh_files,rfid_modes,'cc2-rfid')
            guard=root/'installer-barrier'
            subprocess.run(['arm-linux-gnueabihf-gcc','-O2','-Wall','-Wextra','-Werror','-static',
                '-DNB_BARRIER='+json.dumps(str(fixture/'lock')),HERE.parents[1]/'cavity-fan-floor/installer_barrier.c','-o',guard],check=True,timeout=120)
            library=Path('/qualified-floor/libcc2-cavity-floor-r1.so').read_bytes()
            assert sha(library)=='52d400605dd51f89eb95248c42a1661b3c0e60fadec5453d27a987729332fb10'
            manager=f'''#!/bin/sh
INSTALLER_GUARD_SHA={sha(guard.read_bytes())}
qemu-arm '{floor_root}/installer-barrier' --check "$2" || exit 1
case "$1" in
check-from-installer) echo floor-check >> '{fixture}/trace';;
enable-from-installer) echo floor-enable >> '{fixture}/trace';;
*) exit 1;;
esac
'''.encode()
            floor_files={n:b'inert floor fixture' for n in ['activate.sh','launcher.sh','profile.original.sha256','profile.sh','reactor-entry.sh','thermal.awk']}
            floor_files.update({'manage.sh':manager,'installer-barrier':guard.read_bytes(),'libcc2-cavity-floor-r1.so':library})
            floor_files['SHA256SUMS']=''.join(sha(b)+'  '+n+'\n' for n,b in sorted(floor_files.items())).encode()
            floor_modes={n:0o700 if n.endswith('.sh') or n=='installer-barrier' else 0o600 for n in floor_files}
            cavity_runtime=publisher_for('floor-publisher',floor_files,floor_modes,'cc2-cavity-floor')
            entries={'rfid.tar.gz':rfid_runtime,'cavity.tar.gz':cavity_runtime,'binding.tar.gz':bundle.read_bytes()}
            entries['delivery.json']=json.dumps({'protocol':1,'app_sha256':app_sha,
                'runtime_sha256':{n:sha(b) for n,b in entries.items()}}).encode()
            combined=io.BytesIO()
            with tarfile.open(fileobj=combined,mode='w:gz') as archive:
                for name,data in entries.items():
                    item=tarfile.TarInfo(name);item.size=len(data);item.mode=0o600;archive.addfile(item,io.BytesIO(data))
            combined_runtime=root/'native.tar.gz';combined_runtime.write_bytes(combined.getvalue())
        output=root/'update.zip'
        pkg.package(prepared,APP/'scripts',source,output,**({'native_runtime':combined_runtime} if unified else {'app_binding_runtime':bundle}))
        with zipfile.ZipFile(output) as archive:
            payload=archive.read(next(n for n in archive.namelist() if n.endswith('/cc2-control-payload.tar.gz')))
        for shell in [['dash'],['busybox','sh']]:
            for case in (['success','unsafe-idle','commit-failure','health-failure','rollback-failure','tampered-tail','foreign-destination','native-changed','fresh-success','fresh-failure','fresh-without-canvas','fresh-without-canvas-failure','fresh-without-canvas-current','current'] if unified else ['success','unsafe-idle','commit-failure','health-failure','rollback-failure','tampered-tail','foreign-destination','native-changed']):
                if fixture.exists():shutil.rmtree(fixture)
                fixture.mkdir(mode=0o700)
                if unified:(fixture/'opt/usr').mkdir(parents=True,mode=0o700)
                stage=fixture/'stage';stage.mkdir(mode=0o700)
                with tarfile.open(fileobj=io.BytesIO(payload),mode='r:gz') as archive:
                    for entry in archive:put(stage/entry.name,archive.extractfile(entry).read(),entry.mode)
                target=fixture/'app';put(target/'cc2-control',b'OLD APP');put(target/'spools.json',b'PRESERVE')
                if unified and not case.startswith('fresh-'):
                    put(prefix/'cc2-rfid/lifecycle',b'inert active native fixture')
                if case in ['current','fresh-without-canvas-current']:put(target/'cc2-control',binary)
                marker=fixture/'running';marker.touch();trace=fixture/'trace'
                init=fixture/'init';put(init,f'#!/bin/sh\ncase "$1" in start) touch "{marker}";; stop) rm -f "{marker}";; esac\n',0o755)
                put(stage/'cc2-control.init',init.read_bytes(),0o755)
                printer=fixture/'printer';daemon=fixture/'daemon';put(printer,b'printer data');put(daemon,b'daemon data')
                put(dependency,b'altered' if case=='native-changed' else b'native unchanged')
                put(fixture/'native-journal',b'NATIVE JOURNAL PRESERVED')
                if case=='foreign-destination':put(destination/'foreign',b'KEEP EVIDENCE')
                commands=fixture/'bin';commands.mkdir()
                connected_marker=fixture/'canvas-connected'
                if not case.startswith('fresh-without-canvas'):connected_marker.touch()
                put(commands/'native-probe',f'''#!/bin/sh
[ "$#" -eq 2 ] || exit 90
[ "$(sha256sum "$1" | cut -d ' ' -f 1)" = '{app_sha}' ] || exit 91
case "$2" in
    --installer-native-idle) [ '{case}' != unsafe-idle ] || exit 3;;
    --installer-native-canvas) [ -f '{connected_marker}' ] || exit 2;;
    *) exit 92;;
esac
''',0o755)
                health=json.dumps({'service':'cc2-control','version':pkg.VERSION if case not in ['health-failure','fresh-failure','fresh-without-canvas-failure'] else 'wrong',
                    'mqtt_registered':True,'snapshot_received':True},separators=(',',':'))
                status='{"connected":true,"last_message_age":1,"extruder":{"temperature":24,"target":0},"heater_bed":{"temperature":22,"target":0},"machine":{"status":1,"sub_status":0}}'
                canvas=json.dumps({'available':not case.startswith('fresh-without-canvas'),'telemetry':{'canvas_list':[{'connected':0 if case.startswith('fresh-without-canvas') else 1}]}},separators=(',',':'))
                put(commands/'wget',f'#!/bin/sh\ncase "$*" in */api/canvas*) echo \'{canvas}\';; */api/health*) echo \'{health}\';; *) echo \'{status}\';; esac\n',0o755)
                put(commands/'pidof',f'#!/bin/sh\ncase "$1" in elegoo_printer) echo 200;; cc2-control) [ -f "{marker}" ] && echo 100;; esac\n',0o755)
                put(commands/'sleep','#!/bin/sh\n[ "$1" != 2 ] || exit 0\nexec /bin/sleep "$@"\n',0o755)
                memory=fixture/'meminfo';put(memory,b'MemAvailable: 65000 kB\n')
                replacements={'/opt/usr/cc2-control':str(target),'/etc/init.d/cc2-control':str(init),
                    '/tmp/cc2-control-install.lock':str(fixture/'lock'),'/opt/usr/cc2-rfid-app-updates':str(updates),
                    '/opt/bin/elegoo_printer':str(printer),'/opt/inst/daemon-000/daemon-000':str(daemon),
                    '/proc/$printer_pid/exe':str(printer),'/proc/$RUN_PID/exe':str(target/'cc2-control'),
                    '/opt/usr/cc2-rfid':str(prefix/'cc2-rfid') if unified else '/opt/usr/cc2-rfid',
                    '/opt/usr/cc2-cavity-floor':str(floor_root) if unified else '/opt/usr/cc2-cavity-floor',
                    '/proc/meminfo':str(memory),pkg.COMPATIBILITY['printer_sha256']:sha(printer.read_bytes()),
                    pkg.COMPATIBILITY['daemon_sha256']:sha(daemon.read_bytes())}
                script=(stage/'install-on-printer.sh').read_text()
                for before,after in replacements.items():script=script.replace(before,after)
                # The mock ELF stays in place for checksum and copy checks.
                # Only intercept the direct-console observation at its CLI boundary.
                for option in ('--installer-native-idle','--installer-native-canvas'):
                    script=script.replace('"$STAGE/cc2-control" '+option,'native-probe "$STAGE/cc2-control" '+option)
                # Run the real ARM publishers and worker double via emulation.
                for name in ['CAVITY_SOURCE','RFID_SOURCE']:
                    for action in ['--publish','--retire-dormant']:
                        script=script.replace('"$'+name+'/publisher" '+action,'qemu-arm "$'+name+'/publisher" '+action)
                script=script.replace('"$BINDING_SOURCE/component/worker" --check-current','qemu-arm "$BINDING_SOURCE/component/worker" --check-current')
                script=script.replace('"$BINDING_SOURCE/publisher" --publish','qemu-arm "$BINDING_SOURCE/publisher" --publish')
                for action in ['--prepare','--commit','--rollback']:
                    script=script.replace(str(destination)+'/worker '+action,'qemu-arm '+str(destination)+'/worker '+action)
                script=script.replace('"$tries" -lt 120','"$tries" -lt 2')
                if case=='tampered-tail':
                    marker=script.index('\n# CC2_APP_BINDING_DATA_BEGIN\n')
                    script=script[:marker]+script[marker:].replace("printf '\\037", "printf '\\000",1)
                put(stage/'install-on-printer.sh',script,0o755)
                (stage/'SHA256SUMS').write_text(''.join(sha(p.read_bytes())+'  '+p.relative_to(stage).as_posix()+'\n'
                    for p in sorted(stage.rglob('*')) if p.is_file() and p.name!='SHA256SUMS'))
                env=dict(os.environ,PATH=str(commands)+':'+os.environ['PATH'],TRACE=str(trace),MARKER=str(marker),
                    BINDING_FAIL='rollback' if case=='rollback-failure' else 'commit' if case=='commit-failure' else '')
                if case=='rollback-failure':
                    health=health.replace(pkg.VERSION,'wrong')
                    put(commands/'wget',f'#!/bin/sh\ncase "$*" in */api/canvas*) echo \'{canvas}\';; */api/health*) echo \'{health}\';; *) echo \'{status}\';; esac\n',0o755)
                result=subprocess.run([*shell,stage/'install-on-printer.sh'],env=env,capture_output=True,text=True,timeout=90)
                assert (result.returncode==0)==(case in ['success','fresh-success','fresh-without-canvas','fresh-without-canvas-current','current']),(shell,case,result.stdout,result.stderr)
                calls=trace.read_text().splitlines() if trace.exists() else []
                if case=='fresh-success':assert calls==['floor-check','floor-enable'] and (prefix/'cc2-rfid').exists()
                if case.startswith('fresh-without-canvas'):
                    assert not (prefix/'cc2-rfid').exists() and calls==['floor-check','floor-enable'],(shell,case,calls)
                    assert not destination.exists(),'deferred RFID must not publish a binding worker'
                    assert 'RFID DEFERRED:' in result.stdout
                    if case=='fresh-without-canvas-failure':assert (target/'cc2-control').read_bytes()==b'OLD APP'
                    else:assert (target/'cc2-control').read_bytes()==binary
                if case=='fresh-failure':assert not (prefix/'cc2-rfid').exists() and list(prefix.glob('.cc2-rfid-dormant-*')) and (target/'cc2-control').read_bytes()==b'OLD APP'
                if case=='current':assert all(c=='--check-current' for c in calls) and len(calls)==3
                if case=='success':assert calls==['--prepare','--commit'] and (target/'cc2-control').read_bytes()==binary
                if case in ['commit-failure','health-failure']:
                    assert calls[-1]=='--rollback' and (target/'cc2-control').read_bytes()==b'OLD APP' and marker.exists(),(shell,case,calls)
                if case=='rollback-failure':
                    assert calls[-1]=='--rollback' and (target/'cc2-control').read_bytes()==binary and not marker.exists()
                    assert (fixture/'lock').exists(),'uncertain rollback must retain barrier'
                else:assert not (fixture/'lock').exists()
                if case in ['unsafe-idle','tampered-tail','foreign-destination','native-changed']:assert not calls and (target/'cc2-control').read_bytes()==b'OLD APP'
                if case=='unsafe-idle':assert not list(fixture.glob('app-backup-*'))
                if unified and not case.startswith('fresh-'):assert not floor_root.exists(),'active/current route must not stage floor'
                assert (target/'spools.json').read_bytes()==b'PRESERVE'
                assert (fixture/'native-journal').read_bytes()==b'NATIVE JOURNAL PRESERVED'
                if case=='success':
                    backup=next(fixture.glob('app-backup-*'))
                    restore=(backup/'restore.sh').read_text()
                    for before,after in replacements.items():restore=restore.replace(before,after)
                    restore=restore.replace('"$BACKUP/native-check-worker" --check-current','qemu-arm "$BACKUP/native-check-worker" --check-current')
                    restore=restore.replace(str(destination)+'/worker --rollback','qemu-arm '+str(destination)+'/worker --rollback')
                    put(backup/'restore.sh',restore,0o755)
                    restored=subprocess.run([*shell,backup/'restore.sh'],env=env,capture_output=True,text=True,timeout=30)
                    assert restored.returncode==0,(shell,'backup restore',restored.stdout,restored.stderr)
                    assert (target/'cc2-control').read_bytes()==b'OLD APP' and marker.exists()
                    assert trace.read_text().splitlines()==['--prepare','--commit','--rollback']
                    assert not (fixture/'lock').exists()
                if case=='fresh-without-canvas':
                    # Restore the ordinary app while no RFID lifecycle exists.
                    backup=next(fixture.glob('app-backup-*'))
                    restore=(backup/'restore.sh').read_text()
                    for before,after in replacements.items():restore=restore.replace(before,after)
                    put(backup/'restore.sh',restore,0o755)
                    restored=subprocess.run([*shell,backup/'restore.sh'],env=env,capture_output=True,text=True,timeout=30)
                    assert restored.returncode==0,(shell,'no-Canvas restore',restored.stdout,restored.stderr)
                    assert (target/'cc2-control').read_bytes()==b'OLD APP' and not (prefix/'cc2-rfid').exists()
                    # The same installer can be rerun, then can stage RFID once
                    # a connected accessory is observed. No separate package.
                    repeated=subprocess.run([*shell,stage/'install-on-printer.sh'],env=env,capture_output=True,text=True,timeout=90)
                    assert repeated.returncode==0 and not (prefix/'cc2-rfid').exists(),(shell,'no-Canvas repeat',repeated.stdout,repeated.stderr)
                    connected_marker.touch()
                    connected=subprocess.run([*shell,stage/'install-on-printer.sh'],env=env,capture_output=True,text=True,timeout=90)
                    assert connected.returncode==0 and (prefix/'cc2-rfid').exists(),(shell,'later Canvas connection',connected.stdout,connected.stderr)
                    assert trace.read_text().splitlines()==['floor-check','floor-enable']*3
                    count+=3
                count+=1
    print(f'PASS: {count} '+('unified first-install/active/current ' if unified else '')+f'real ARM publication and dash/BusyBox installer boundary cases; prepare/commit/rollback ordering, failure retention and preserved native journal; worker double at installer boundary only',flush=True)
    return count

if __name__=='__main__':run(unified=sys.argv[1:]==['--unified'])
