import hashlib
import json
import os
from pathlib import Path
import struct
import subprocess
import sys

root=Path(__file__).resolve().parent
os.chdir(root)
out=root/'output'
out.mkdir(exist_ok=True)

def run(args, **kwargs):
    return subprocess.run([str(x) for x in args], check=True, timeout=240, **kwargs)

run([sys.executable, 'test_status.py'])
binary=out/'cc2-network-status'
zig=os.environ['CC2_ZIG']
flags=['-std=c11','-Os','-Wall','-Wextra','-Wpedantic','-Werror','-static','-s']
run([zig,'cc','-target','arm-linux-musleabihf','-mcpu=cortex_a7',*flags,'status.c','-o',binary])
blob=binary.read_bytes()
assert blob[:7]==b'\x7fELF\x01\x01\x01' and struct.unpack_from('<H',blob,18)[0]==40
assert struct.unpack_from('<I',blob,36)[0]&0x400, 'hard-float ARM required'
phoff=struct.unpack_from('<I',blob,28)[0]
phsize,phnum=struct.unpack_from('<HH',blob,42)
assert all(struct.unpack_from('<I',blob,phoff+i*phsize)[0] not in (2,3) for i in range(phnum)), 'static, no interpreter/dynamic segment'
executed=run(['qemu-arm',binary], capture_output=True,text=True)
snapshot=json.loads(executed.stdout)
assert snapshot['read_only'] and snapshot['route']['internet']=='untested'
assert snapshot['capabilities']=={'network_changes':False,'automatic_selection':False}
rejected=subprocess.run(['qemu-arm',binary,'--change'],capture_output=True,timeout=5)
assert rejected.returncode==2 and not rejected.stdout
# Native strace evidence: no process execution, no IP send, no file mutation.
host=out/'status-host'
run(['gcc',*flags,'status.c','-o',host])
trace=out/'read-only-syscalls.txt'
run(['strace','-f','-o',trace,'-e','trace=network,openat,execve,rename,unlink,ioctl',host],stdout=subprocess.DEVNULL)
calls=trace.read_text()
for line in calls.splitlines():
    assert not any(word in line for word in ('O_WRONLY','O_RDWR','O_CREAT','O_TRUNC','SIOCS')),line
    if 'sendto(' in line or 'sendmsg(' in line:
        assert not any(word in line for word in ('AF_INET,','AF_INET6,','RTM_NEW','RTM_DEL')),line
assert sum('execve(' in line for line in calls.splitlines())==1, 'no child commands'
result={'scope':'Read-only network status, not the connection editor',
        'binary_sha256':hashlib.sha256(blob).hexdigest(),'binary_bytes':len(blob),
        'host_core_and_socket_asan_ubsan':True,'arm_static_execution_qemu':True,
        'read_only_syscalls_host':True,'real_printer_qualified':False,
        'network_changes':False,'automatic_selection':False}
(out/'validation.json').write_text(json.dumps(result,indent=2)+'\n')
print(json.dumps(result,indent=2))
