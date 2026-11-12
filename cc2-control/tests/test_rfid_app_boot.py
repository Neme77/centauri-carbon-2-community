"""Actual startup bootstrap and inert ELF worker, no printer or vendor programs."""
from pathlib import Path
import hashlib,json,os,shutil,subprocess,tempfile

APP=Path(__file__).resolve().parents[1]
CASES=['absent','no-plan','valid','public-root','symlink-root','public-plan','symlink-plan',
    'no-descriptor','public-descriptor','symlink-descriptor','hardlink-descriptor','malformed',
    'wrong-app','bad-plan','bad-worker-hash','unknown-field','changed-worker','symlink-worker',
    'hardlink-worker','public-worker','worker-failed','timeout']
sha=lambda b:hashlib.sha256(b).hexdigest()

def put(p,data,mode=0o600):
    p.parent.mkdir(parents=True,exist_ok=True,mode=0o700);p.write_bytes(data);p.chmod(mode)

def run():
    count=0
    with tempfile.TemporaryDirectory(prefix='cc2-app-boot-') as temporary:
        root=Path(temporary);updates=root/'updates';marker=root/'worker-called';source=root/'app.c';helper_source=root/'helper.c'
        source.write_text('#define _GNU_SOURCE\n#include "rfid_app_update_boot.h"\nint main(void){struct rlimit lim={1024,1024};if(setrlimit(RLIMIT_NOFILE,&lim))return 2;return rfid_app_update_boot()?0:1;}\n')
        helpers={}
        for name in ['normal','failed','timeout']:
            helper_source.write_text('#define _GNU_SOURCE\n#include <fcntl.h>\n#include <stdlib.h>\n#include <string.h>\n#include <unistd.h>\nint main(int n,char**v){if(n!=2||strcmp(v[1],"--recover")||getenv("UNTRUSTED_BOOT_ENV"))return 9;int f=open('+json.dumps(str(marker))+',O_WRONLY|O_CREAT|O_EXCL|O_NOFOLLOW,0600);if(f<0||write(f,"called",6)!=6)return 8;close(f);'+('sleep(5);' if name=='timeout' else '')+'return '+('1' if name=='failed' else '0')+';}\n')
            binary=root/('helper-'+name)
            subprocess.run(['gcc','-O2','-std=c11','-Wall','-Wextra','-Wpedantic','-Werror',helper_source,'-o',binary],check=True,timeout=60)
            helpers[name]=binary.read_bytes()
        for arch,compiler,extra,runner in [
            ('host','gcc',[],[]),('arm','arm-linux-gnueabihf-gcc',['-static'],['qemu-arm']),
            ('sanitizers','gcc',['-fsanitize=address,undefined','-fno-sanitize-recover=all','-fno-pie','-no-pie'],[])]:
            binary=root/(arch+'-cc2-control')
            subprocess.run([compiler,'-O2','-std=c11','-Wall','-Wextra','-Wpedantic','-Werror',*extra,
                '-DCC2_RFID_APP_UPDATES='+json.dumps(str(updates)),'-DCC2_RFID_APP_BOOT_MS=1000',
                '-I'+str(APP/'src'),source,'-o',binary],check=True,timeout=120)
            app_sha=sha(binary.read_bytes())
            for case in CASES:
                if updates.is_symlink():updates.unlink()
                if updates.exists():shutil.rmtree(updates)
                saved=root/'saved-root'
                if saved.exists():shutil.rmtree(saved)
                if marker.exists():marker.unlink()
                updates.mkdir(mode=0o700);directory=updates/app_sha;directory.mkdir(mode=0o700)
                mode='failed' if case=='worker-failed' else 'timeout' if case=='timeout' else 'normal'
                worker=directory/'worker';put(worker,helpers[mode],0o700)
                metadata={'protocol':1,'app_sha256':app_sha,'worker_sha256':sha(helpers[mode]),'plan_sha256':'ab'*32}
                if case=='wrong-app':metadata['app_sha256']='cd'*32
                if case=='bad-plan':metadata['plan_sha256']='bad'
                if case=='bad-worker-hash':metadata['worker_sha256']='de'*32
                if case=='unknown-field':metadata['path']='/untrusted'
                descriptor=directory/'recovery.json';put(descriptor,json.dumps(metadata).encode())
                if case=='absent':shutil.rmtree(updates)
                if case=='no-plan':shutil.rmtree(directory)
                if case=='public-root':updates.chmod(0o755)
                if case=='symlink-root':updates.rename(saved);updates.symlink_to(saved,target_is_directory=True)
                if case=='public-plan':directory.chmod(0o755)
                if case=='symlink-plan':directory.rename(updates/'saved');directory.symlink_to(updates/'saved',target_is_directory=True)
                if case=='no-descriptor':descriptor.unlink()
                if case=='public-descriptor':descriptor.chmod(0o644)
                if case=='malformed':put(descriptor,b'{')
                if case in ['symlink-descriptor','hardlink-descriptor']:
                    target=root/'saved-descriptor';put(target,descriptor.read_bytes());descriptor.unlink()
                    if case=='symlink-descriptor':descriptor.symlink_to(target)
                    else:os.link(target,descriptor)
                if case=='changed-worker':put(worker,helpers[mode]+b'changed',0o700)
                if case in ['symlink-worker','hardlink-worker']:
                    target=root/'saved-worker';put(target,worker.read_bytes(),0o700);worker.unlink()
                    if case=='symlink-worker':worker.symlink_to(target)
                    else:os.link(target,worker)
                if case=='public-worker':worker.chmod(0o755)
                result=subprocess.run([*runner,binary],env=dict(os.environ,UNTRUSTED_BOOT_ENV='must-not-inherit'),capture_output=True,text=True,timeout=12)
                okay=case in ['absent','no-plan','valid']
                assert (result.returncode==0)==okay,(arch,case,result.returncode,result.stdout,result.stderr)
                assert marker.exists()==(case in ['valid','worker-failed','timeout']),(arch,case,'unqualified worker executed')
                count+=1
    print(f'PASS: {count} actual app bootstrap host/ARM/sanitizer cases; qualified worker only, no ambient environment, bounded timeout, unsafe/foreign metadata refusal',flush=True)
    return count
if __name__=='__main__':run()
