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
run([sys.executable, 'test_manual_trial.py'])
run([sys.executable, 'test_network_package_checks.py'])

# Integrate into a disposable source copy so the real HTTP handler is tested
# without changing the canonical tree or pretending this is a release package.
import shutil
app=out/'backend-app'
if app.exists():
    assert app.resolve().parent==out.resolve()
    shutil.rmtree(app)
shutil.copytree(root.parents[1]/'cc2-control', app, ignore=shutil.ignore_patterns('node_modules','build','dist'))
run([sys.executable,'integrate-status.py',app])
run([sys.executable,'integrate-ui.py',app])
run(['make','-C',app,'build/cc2-control','CROSS=','CC=gcc',
     'EXTRA_CFLAGS=-DCC2_NETWORK_STATUS_TEST=\\\"/tmp/cc2-network-http-test.sock\\\"'])
run([sys.executable,'test_http_status.py',app/'build/cc2-control'])

# Build and drive the real production UI, not the standalone design prototype.
web=app/'web-src'
run(['npm','ci'],cwd=web)
run(['npm','run','format'],cwd=web)
run(['npm','run','check'],cwd=web)
run(['npm','run','build'],cwd=web)
browser=shutil.which('google-chrome') or shutil.which('google-chrome-stable')
if browser:
    os.environ['CC2_BROWSER_PATH']=browser
else:
    run(['npx','playwright','install','chromium'],cwd=web)
run([sys.executable,'prepare-axe.py'])
os.environ['CC2_AXE_PATH']=str(out/'axe/axe.min.js')
run(['node','test_ui.mjs',app,out/'network-ui'])
run(['node','test_manual_ui.mjs',app,out/'network-ui'])
run(['node','test_accessibility.mjs',app,out/'network-ui'])
run(['gcc','-std=c11','-O1','-UNDEBUG','-Wall','-Wextra','-Wpedantic','-Werror',
     '-fsanitize=address,undefined','-fno-omit-frame-pointer','-fno-pie','-no-pie',
     'test_transaction.c','-o',out/'test-transaction'])
run([out/'test-transaction'])


binary=out/'cc2-network-status'
zig=os.environ['CC2_ZIG']
flags=['-std=c11','-Os','-Wall','-Wextra','-Wpedantic','-Werror','-static','-s']
run([zig,'cc','-target','arm-linux-musleabihf','-mcpu=cortex_a7',*flags,'status.c','-o',binary])
run([zig,'cc','-target','arm-linux-musleabihf','-mcpu=cortex_a7',*flags,'test_transaction.c','-o',out/'test-transaction-arm'])
run(['qemu-arm',out/'test-transaction-arm'])
# Hardware candidate: original CC2 sources plus the read-only adapter/UI.
# Never pass the test socket macro to this build.
arm_app=out/'cc2-control-network-ui'
version=(app/'VERSION').read_text().strip()
firmware=(app/'FIRMWARE_VERSION').read_text().strip()
run(['make','-C',app,'clean'])
run(['make','-C',app,'build/cc2-control'])
shutil.copyfile(app/'build/cc2-control',arm_app)
assert b'/tmp/cc2-network-http-test.sock' not in arm_app.read_bytes()
env=dict(os.environ,CC2_TEST_RUNNER='qemu-arm')
run([sys.executable,app/'tests/test_smoke.py',arm_app],env=env)
# Ship exact integrated sources alongside the candidate; they can be rebuilt
# using this committed integration recipe. This is not a builder release.
import zipfile
with zipfile.ZipFile(out/'integrated-sources.zip','w',compression=zipfile.ZIP_DEFLATED) as archive:
    for p in sorted(app.rglob('*')):
        rel=p.relative_to(app)
        if p.is_file() and not any(part in ('node_modules','build','dist','.git') for part in rel.parts):
            archive.write(p,'cc2-control/'+rel.as_posix())
    for p in sorted(root.iterdir()):
        if p.is_file() and (p.suffix in ('.c','.h','.py','.tsx','.json','.mjs','.md') or p.name in ('THIRD-PARTY-NOTICES.txt','LICENSE.GPL-3.0')):
            archive.write(p,'network-development/'+p.name)
    for p in sorted((root/'manual-trial').rglob('*')):
        if p.is_file():archive.write(p,'network-development/manual-trial/'+p.relative_to(root/'manual-trial').as_posix())
    archive.write(root.parents[1]/'LICENSE','LICENSE.GPL-3.0')
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
        'app_compiler':run(['arm-linux-gnueabihf-gcc','--version'],capture_output=True,text=True).stdout.splitlines()[0],
        'app_build':'canonical Makefile / ARM GNU static glibc',
        'binary_sha256':hashlib.sha256(blob).hexdigest(),'binary_bytes':len(blob),
        'host_core_and_socket_asan_ubsan':True,'arm_static_execution_qemu':True,
        'read_only_syscalls_host':True,'actual_http_status_adapter':True,'manual_trial_bridge_host_test':True,'manual_trial_physical_test':False,'production_ui_nine_languages':True,'transaction_policy_only':True,'real_printer_qualified':False,
        'network_changes':False,'automatic_selection':False,
        'app_binary_sha256':hashlib.sha256(arm_app.read_bytes()).hexdigest(),
        'integrated_sources_sha256':hashlib.sha256((out/'integrated-sources.zip').read_bytes()).hexdigest(),
        'source_sha256':{p.name:hashlib.sha256(p.read_bytes()).hexdigest() for p in root.iterdir() if p.is_file() and p.suffix in ('.c','.h','.py','.tsx','.json','.mjs','.md')}}
(out/'validation.json').write_text(json.dumps(result,indent=2)+'\n')
print(json.dumps(result,indent=2))
