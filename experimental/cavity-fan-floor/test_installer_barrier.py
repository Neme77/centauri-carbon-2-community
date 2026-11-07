"""Actual shared installer descriptor guard on host, ARM and sanitizers."""
from pathlib import Path
import json,os,subprocess,tempfile

HERE=Path(__file__).resolve().parent


def run():
    count=0
    with tempfile.TemporaryDirectory(prefix='cavity-installer-guard-') as temporary:
        root=Path(temporary)
        for arch,compiler,flags,runner in [('host','gcc',[],[]),
            ('arm','arm-linux-gnueabihf-gcc',['-static','-D_TIME_BITS=32','-D_FILE_OFFSET_BITS=32'],['qemu-arm']),
            ('sanitizers','gcc',['-fsanitize=address,undefined','-fno-sanitize-recover=all','-fno-pie','-no-pie'],[])]:
            shared=root/(arch+'-shared');binary=root/(arch+'-guard')
            subprocess.run([compiler,'-O2','-std=c11','-Wall','-Wextra','-Wpedantic','-Werror',*flags,
                '-DNB_BARRIER='+json.dumps(str(shared)),HERE/'installer_barrier.c','-o',binary],check=True,timeout=120)
            for case in ['valid','missing','foreign','replaced','symlink','public','closed','stdin']:
                shared.mkdir(mode=0o700);held=os.open(shared,os.O_RDONLY|os.O_DIRECTORY)
                other=root/(arch+'-'+case+'-other')
                try:
                    number=str(held)
                    if case=='missing':shared.rmdir()
                    if case=='foreign':
                        other.mkdir(mode=0o700);os.close(held);held=os.open(other,os.O_RDONLY|os.O_DIRECTORY);number=str(held)
                    if case=='replaced':shared.rename(other);shared.mkdir(mode=0o700)
                    if case=='symlink':shared.rename(other);shared.symlink_to(other,target_is_directory=True)
                    if case=='public':shared.chmod(0o755)
                    if case=='closed':number='999999'
                    if case=='stdin':number='0'
                    result=subprocess.run([*runner,binary,'--check',number],pass_fds=(held,),capture_output=True,text=True,timeout=15)
                    assert (result.returncode==0)==(case=='valid'),(arch,case,result.stdout,result.stderr)
                finally:
                    os.close(held)
                    if shared.is_symlink():shared.unlink()
                    elif shared.exists():shared.rmdir()
                    if other.exists():other.rmdir()
                count+=1
    print(f'PASS: {count} actual installer descriptor identity cases on host/ARM/sanitizers',flush=True)
    return count

if __name__=='__main__':run()
