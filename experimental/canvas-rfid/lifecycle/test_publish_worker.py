"""Actual dormant component publication, host/ARM/ASan, filesystem only."""
from pathlib import Path
import hashlib
import json
import os
import shutil
import subprocess
import tempfile
from publish_bundle import configuration

HERE=Path(__file__).resolve().parent
sha=lambda b:hashlib.sha256(b).hexdigest()
CASES=['check','publish','retry','source-changed','source-symlink','source-public',
    'dependency-changed','dependency-symlink','dependency-mode','busy-lock-absent','lock-symlink',
    'foreign-root','root-symlink','active-journal','interrupted-copy','interrupted-publish','name-race']

def put(path,data,mode=0o600):
    path.parent.mkdir(parents=True,exist_ok=True,mode=0o700);path.write_bytes(data);path.chmod(mode)

def run(target_name="cc2-rfid",app_binding=False):
    count=0
    with tempfile.TemporaryDirectory(prefix='cc2-publish-') as temporary:
        root=Path(temporary)
        fault=root/'fault.h'
        fault.write_text('#define _GNU_SOURCE\n#include <stdlib.h>\n#include <string.h>\n#include <unistd.h>\n#include <sys/stat.h>\nstatic void cp_test_fault(const char*p){const char*v=getenv("CP_CRASH");if(v&&!strcmp(v,p))_exit(91);if(v&&!strcmp(v,"name-race")&&!strcmp(p,"before-publish")){if(mkdir(CP_PARENT "/" CP_TARGET,0700))_exit(93);}}\n#define CP_FAULT(p) cp_test_fault(p)\n')
        files={'enabled':b'CC2-RFID-NATIVE-R2\n','app-integration.json':b'private fixture',
               'activate-on-ready':b'fixture request','payload/0':b'future root delegate',
               'recover-worker':bytes(range(256))*1024}
        modes={n:(0o700 if n=='recover-worker' else 0o600) for n in files}
        probe='enabled'
        if app_binding:
            files={'worker':bytes(range(256))*1024,'recovery.json':b'closed fixture',
                'incoming/coordinator':b'new coordinator','incoming/capture':b'new capture',
                'incoming/app-integration.json':b'new metadata','incoming/activate-on-ready':b'new request'}
            modes={n:0o700 if n in ['worker','incoming/coordinator','incoming/capture'] else 0o600 for n in files}
            probe='incoming/coordinator'
        config=root/'configuration.h';config.write_bytes(configuration(files,modes,[('/etc/old-startup',sha(b'previous startup'),0o755)],app_binding=app_binding))
        for arch,compiler,flags,runner in [
            ('host','gcc',[],[]),('arm','arm-linux-gnueabihf-gcc',['-static'],['qemu-arm']),
            ('sanitizers','gcc',['-fsanitize=address,undefined','-fno-sanitize-recover=all','-fno-pie','-no-pie'],[])]:
            fixture=root/arch;parent=fixture/'parent';source=fixture/'source';lock=fixture/'lock';tree=fixture/'tree'
            binary=root/(arch+'-publisher')
            defines={'CP_PARENT':str(parent),'CP_PREFIX':str(tree),'CP_LOCK':str(lock),'CP_CONFIG':str(config),'CP_TARGET':target_name}
            subprocess.run([compiler,'-O2','-std=c11','-Wall','-Wextra','-Wpedantic','-Werror','-D_POSIX_C_SOURCE=200809L',
                *flags,*(['-DCP_APP_BINDING=1'] if app_binding else []),*['-D'+k+'='+json.dumps(v) for k,v in defines.items()],'-include',fault,HERE/'publish_worker.c','-o',binary],check=True,timeout=120)
            for case in CASES:
                if fixture.exists():shutil.rmtree(fixture)
                parent.mkdir(parents=True,mode=0o700);source.mkdir(mode=0o700);lock.mkdir(mode=0o700)
                dependency=tree/'etc/old-startup';put(dependency,b'previous startup',0o755)
                for name,data in files.items():put(source/name,data,modes[name])
                target=parent/target_name
                def invoke(action,env=None):
                    return subprocess.run([*runner,binary,action,source],env=env,capture_output=True,text=True,timeout=15)
                if case in ['retry','active-journal']:
                    result=invoke('--publish');assert result.returncode==0,(arch,case,result.stderr)
                if case=='active-journal':put(target/'lifecycle',b'foreign or active evidence')
                if case=='source-changed':put(source/probe,b'changed')
                if case=='source-symlink':
                    (source/probe).unlink();(source/probe).symlink_to(dependency)
                if case=='source-public':(source/probe).chmod(0o666)
                if case=='dependency-changed':put(dependency,b'changed',0o755)
                if case=='dependency-symlink':dependency.unlink();dependency.symlink_to(source/probe)
                if case=='dependency-mode':dependency.chmod(0o700)
                if case=='busy-lock-absent':lock.rmdir()
                if case=='lock-symlink':lock.rmdir();lock.symlink_to(source,target_is_directory=True)
                if case=='foreign-root':target.mkdir(mode=0o700);put(target/'foreign',b'preserve')
                if case=='root-symlink':target.symlink_to(source,target_is_directory=True)
                env=dict(os.environ)
                if case=='interrupted-copy':env['CP_CRASH']='copied'
                if case=='interrupted-publish':env['CP_CRASH']='published'
                if case=='name-race':env['CP_CRASH']='name-race'
                action='--check' if case=='check' else '--publish'
                result=invoke(action,env)
                success=case in ['check','publish','retry']
                assert (result.returncode==0)==success,(arch,case,result.stdout,result.stderr)
                if case.startswith('interrupted-'):
                    assert result.returncode==91
                    retry=invoke('--publish');assert retry.returncode==0,(arch,case,retry.stdout,retry.stderr)
                    success=True
                if success and case!='check':
                    for name,data in files.items():
                        assert (target/name).read_bytes()==data and (target/name).stat().st_mode&0o777==modes[name]
                    assert target.stat().st_mode&0o777==0o700
                if case=='check':assert not target.exists()
                if case=='name-race':assert target.is_dir() and not list(target.iterdir())
                if case=='foreign-root':assert (target/'foreign').read_bytes()==b'preserve'
                if case=='active-journal':assert (target/'lifecycle').read_bytes()==b'foreign or active evidence'
                assert lock.exists() or case=='busy-lock-absent'
                count+=1
    print(f'PASS: {count} actual dormant publication host/ARM/sanitizer cases; no-replace rename, interrupted retry, foreign/active evidence refusal, exact dependencies and retained installer lock')
    return count

if __name__=='__main__':run()
