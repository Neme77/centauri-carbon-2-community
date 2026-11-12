"""Actual host staging coordinator with inert mapped-file processes.

Only the stock firmware DATA is real. No ELEGOO executable/library is executed,
no serial device exists in the fixtures and no firmware transfer is invoked.
"""
from pathlib import Path
import hashlib
import json
import os
import shutil
import struct
import subprocess
import tempfile
import time

HERE=Path(__file__).resolve().parent
sha=lambda data:hashlib.sha256(data).hexdigest()
PATHS=['/etc/init.d/cc2-reactor-launch','/opt/usr/cc2-reactor-runtime-v2/launcher.sh',
       '/opt/usr/cc2-reactor-runtime-v2/SHA256SUMS','/opt/usr/cc2-network-manager/profile.sh',
       '/opt/usr/cc2-network-manager/reactor-entry.sh','/opt/usr/cc2-network-manager/activate.sh',
       '/opt/usr/cc2-network-manager/SHA256SUMS','/opt/usr/cc2-network-manager/startup.sha256']
MODULES=['/opt/usr/cc2-reactor-runtime-v2/libcc2-reactor-v2.so',
         '/opt/usr/cc2-cavity-floor/libcc2-cavity-floor-r1.so',
         '/opt/usr/cc2-network-manager/libcc2-network-compat-r1.so',
         '/opt/usr/cc2-rfid-frame-fix-r1/libcc2-rfid-native-r2.so']
CASES=['check','stage','retry','partial-resume','staged-old-boot','busy','profile-changed','payload-changed',
       'old-target-changed','stock-bad','backup-bad','journal-bad','journal-active',
       'native-phase','active-scan','ready-missing','native-missing','app-missing',
       'duplicate-native','foreign-module','missing-cavity','module-changed',
       'first-guard-refused','second-guard-refused','guard-timeout','cancel-guard','route-changed']

def put(path,data,mode=0o600):
    path.parent.mkdir(parents=True,exist_ok=True,mode=0o700);path.write_bytes(data);path.chmod(mode)

def fixture(folder,dummy,stock):
    component=folder/'component';tree=folder/'tree';component.mkdir(parents=True,mode=0o700)
    old=[];new=[];modes=[0o755,0o700,0o600,0o700,0o700,0o700,0o644,0o600];rows=[]
    for i,path in enumerate(PATHS):
        old.append(f'old startup {i}'.encode());new.append(f'new startup {i}'.encode())
        put(tree/path[1:],old[i],modes[i]);put(component/f'payload/{i}',new[i])
        rows.append(f'{i} {modes[i]:04o} {modes[i]:04o} {sha(old[i])} {sha(new[i])}\n')
    for name in ['boot-dispatch.sh','stock-entry.sh']:put(component/name,b'#!/bin/sh\nexit 125\n',0o700)
    plan=(b'CC2-STARTUP-PLAN-1\n'+f'dispatch {sha((component/"boot-dispatch.sh").read_bytes())}\nstock-entry {sha((component/"stock-entry.sh").read_bytes())}\n'.encode()+''.join(rows).encode())
    put(component/'startup-plan',plan)
    native=tree/'opt/usr/cc2-reactor-runtime-v2/elegoo_printer';app=tree/'opt/usr/cc2-control/cc2-control'
    put(native,dummy,0o700);put(app,dummy,0o700)
    put(tree/'opt/bin/run_printer.sh',b'inert canonical run fixture\n',0o755)
    for i,path in enumerate(MODULES):put(tree/path[1:],f'inert mapped module {i}'.encode()*64)
    for name in ['libcc2-rfid.so','guarded-entry.sh','recover-worker','enabled']:put(component/name,('inert '+name).encode())
    roles=[('bootstrap',component/'startup-plan'),('native',native),('guard',component/'libcc2-rfid.so'),
           ('entry',component/'guarded-entry.sh'),('recover',component/'recover-worker'),('marker',component/'enabled')]
    profile=''.join(f'{role} {sha(path.read_bytes())} {path}\n' for role,path in roles).encode();put(component/'recovery-profile',profile)
    fresh=b'''#!/bin/sh
set -eu
[ "$#" = 1 ] && [ "$1" = --check ] || exit 1
n=0;[ ! -f "$SC_TRACE" ] || n=$(cat "$SC_TRACE")
n=$((n+1));printf '%s' "$n" > "$SC_TRACE"
case "$SC_CASE" in
 first-guard-refused) exit 1;;
 second-guard-refused) [ "$n" -ne 2 ] || exit 1;;
 guard-timeout|cancel-guard) sleep 10;;
esac
exit 0
'''
    put(component/'fresh-guard.sh',fresh,0o700);put(folder/'stock.bin',stock,0o664)
    return component,tree,native,app,old,new,profile,fresh

def seed_journal(component,profile,phase):
    data=bytearray(128);data[:14]=b'CC2-CANVAS-LC1';data[16]=phase
    struct.pack_into('<Q',data,24,1);data[32:48]=bytes(range(1,17))
    data[48:84]=b'00000000-0000-0000-0000-000000000001';data[84:116]=hashlib.sha256(profile).digest()
    put(component/'lifecycle',data+sha(data).encode())

def spawn(path,args):
    process=subprocess.Popen([str(path),*map(str,args)],stdout=subprocess.PIPE,text=True)
    assert process.stdout.readline().strip()=='READY'
    return process

def run(program=None,stock_bootstrap=False,with_cavity=False):
    if program is None:program=HERE/'stage_worker.c'
    stock=Path('/stock-canvas.bin').read_bytes();assert sha(stock)=='f1267ee3219dcad73efffa8dc38e30ff0f17c02c0a1674cbead48a99bdff3228'
    count=0
    cases=CASES if not stock_bootstrap else [c for c in CASES if c not in ['native-phase','active-scan','missing-cavity']]+['unexpected-cavity','unexpected-legacy','missing-reactor']
    if with_cavity:
        assert stock_bootstrap
        cases=[c for c in cases if c!='unexpected-cavity']+['cavity-present']
    flags=['-O2','-std=c11','-Wall','-Wextra','-Wpedantic','-Werror','-D_POSIX_C_SOURCE=200809L']
    with tempfile.TemporaryDirectory(prefix='cc2-host-stage-') as temporary:
        root=Path(temporary);dummy_source=root/'dummy.c';dummy=root/'dummy'
        dummy_source.write_text('#include <fcntl.h>\n#include <stdio.h>\n#include <sys/mman.h>\n#include <unistd.h>\nint main(int n,char**v){for(int i=1;i<n;i++){int f=open(v[i],O_RDONLY);if(f<0||mmap(0,4096,PROT_READ,MAP_PRIVATE,f,0)==MAP_FAILED)return 2;close(f);}puts("READY");fflush(stdout);for(;;)pause();}\n')
        subprocess.run(['gcc',*flags,dummy_source,'-o',dummy],check=True,timeout=60);dummy_bytes=dummy.read_bytes()
        fault=root/'fault.h'
        fault.write_text('#define _GNU_SOURCE\n#include <stdlib.h>\n#include <string.h>\n#include <unistd.h>\nstatic void stage_test_fault(const char*p,unsigned i){const char*v=getenv("SC_TX_EXIT");if(v&&!strcmp(v,p)&&i==0)_exit(91);}\n#define TX_FAULT(p,i) stage_test_fault(p,i)\n')
        for arch,compiler,extra,runner in [
            ('host','gcc',[],[]),('arm','arm-linux-gnueabihf-gcc',['-static','-D_TIME_BITS=32','-D_FILE_OFFSET_BITS=32'],['qemu-arm']),
            ('sanitizers','gcc',['-fsanitize=address,undefined','-fno-sanitize-recover=all','-fno-pie','-no-pie'],[]),
        ]:
            folder=root/arch
            component,tree,native,app,old,new,profile,fresh=fixture(folder,dummy_bytes,stock)
            defines={'SC_BASE':str(component),'SC_PREFIX':str(tree),'SC_PROFILE_SHA':sha(profile),
                'SC_APP_SHA':sha(dummy_bytes),'SC_NATIVE_SHA':sha(dummy_bytes),'SC_FRESH_SHA':sha(fresh),
                'SC_RUN_SHA':sha((tree/'opt/bin/run_printer.sh').read_bytes()),
                'SC_READY':str(folder/'ready'),'SC_ORIGINAL':str(folder/'stock.bin'),
                'CC2_MAINTENANCE_LOCK':str(folder/'install.lock')}
            for key,path in zip(['SC_REACTOR_SHA','SC_CAVITY_SHA','SC_NETWORK_SHA','SC_OLD_RFID_SHA'],MODULES):defines[key]=sha((tree/path[1:]).read_bytes())
            binary=root/(arch+'-stage')
            subprocess.run([compiler,*flags,*extra,'-DSC_GUARD_TIMEOUT_MS=500',
                *['-D'+k+'='+json.dumps(v) for k,v in defines.items()],
                *(['-DSC_STOCK_BOOTSTRAP=1',f'-DSC_WITH_CAVITY={int(with_cavity)}',*(['-DSC_CAVITY_BOOTSTRAP_OPTIONAL=1'] if with_cavity else [])] if stock_bootstrap else []),'-include',fault,
                '-I'+str(HERE.parent/'host'),program,'-lm','-o',binary],check=True,timeout=120)
            for case in cases:
                shutil.rmtree(folder);component,tree,native,app,old,new,profile,fresh=fixture(folder,dummy_bytes,stock)
                native_args=[tree/p[1:] for p in MODULES]
                if stock_bootstrap:
                    native_args=[tree/MODULES[i][1:] for i in [0,2]]
                    if case=='cavity-present':native_args.append(tree/MODULES[1][1:])
                    if case=='unexpected-cavity':native_args.append(tree/MODULES[1][1:])
                    if case=='unexpected-legacy':native_args.append(tree/MODULES[3][1:])
                    if case=='missing-reactor':native_args.remove(tree/MODULES[0][1:])
                if case=='missing-cavity':native_args.remove(tree/MODULES[1][1:])
                if case=='foreign-module':
                    foreign=folder/'libcc2-foreign.so';put(foreign,b'inert foreign mapping'*64);native_args.append(foreign)
                processes=[]
                try:
                    n=spawn(native,native_args);processes.append(n)
                    a=spawn(app,[]);processes.append(a)
                    if case=='duplicate-native':processes.append(spawn(native,native_args))
                    ready=struct.pack('<8sQQQII',b'CC2RF1\0\0',123,321 if case=='active-scan' else 0,int(time.monotonic())+60 if case=='active-scan' else 0,n.pid,2 if case=='native-phase' else 1)
                    if not stock_bootstrap:put(folder/'ready',ready)
                    if case=='native-missing':n.terminate();n.wait(timeout=5)
                    if case=='app-missing':a.terminate();a.wait(timeout=5)
                    if case=='ready-missing' and not stock_bootstrap:(folder/'ready').unlink()
                    if case=='module-changed':put(tree/MODULES[0 if stock_bootstrap else 3][1:],b'changed module'*128)
                    if case=='busy':(folder/'install.lock').mkdir(mode=0o700)
                    if case=='profile-changed':put(component/'recovery-profile',profile+b'changed\n')
                    if case=='payload-changed':put(component/'payload/7',b'changed')
                    if case=='old-target-changed':put(tree/PATHS[7][1:],b'foreign target')
                    if case=='route-changed':put(tree/'opt/bin/run_printer.sh',b'changed route',0o755)
                    if case=='stock-bad':put(folder/'stock.bin',stock[:-1])
                    if case=='backup-bad':put(component/'original-stock-44.bin',b'bad')
                    if case=='journal-bad':put(component/'lifecycle',b'bad')
                    if case=='journal-active':seed_journal(component,profile,5)
                    if case=='staged-old-boot':seed_journal(component,profile,1)
                    before=[(tree/p[1:]).read_bytes() for p in PATHS]
                    env=dict(os.environ,SC_CASE=case,SC_TRACE=str(folder/'fresh-count'))
                    action='--check' if case=='check' else '--stage-host'
                    if case=='partial-resume':
                        crashed=subprocess.run([*runner,binary,action],env=dict(env,SC_TX_EXIT='renamed'),capture_output=True,text=True,timeout=10)
                        assert crashed.returncode==91,(arch,case,crashed.stdout,crashed.stderr)
                        assert (component/'lifecycle').read_bytes()[16]==1
                        assert (tree/PATHS[0][1:]).read_bytes()==new[0]
                        # Simulate the volatile installer lock disappearing at a
                        # complete boot. Production never takes over stale locks.
                        os.rmdir(folder/'install.lock');(folder/'fresh-count').unlink()
                    if case=='cancel-guard':
                        running=subprocess.Popen([*runner,binary,action],env=env,stdout=subprocess.PIPE,stderr=subprocess.PIPE,text=True)
                        deadline=time.monotonic()+5
                        while not (folder/'fresh-count').exists() and time.monotonic()<deadline:time.sleep(.01)
                        assert (folder/'fresh-count').exists();running.terminate();stdout,stderr=running.communicate(timeout=5)
                        result=subprocess.CompletedProcess([],running.returncode,stdout,stderr)
                    else:result=subprocess.run([*runner,binary,action],env=env,capture_output=True,text=True,timeout=10)
                    okay=case in ['check','stage','retry','partial-resume','staged-old-boot'] or (stock_bootstrap and case=='ready-missing') or (with_cavity and case=='cavity-present')
                    assert (result.returncode==0)==okay,(arch,case,result.returncode,result.stdout,result.stderr)
                    if okay and case!='check':
                        assert [(tree/p[1:]).read_bytes() for p in PATHS]==new
                        assert (component/'lifecycle').read_bytes()[16]==1
                        assert (component/'original-stock-44.bin').read_bytes()==stock
                        if case=='staged-old-boot':
                            after=(component/'lifecycle').read_bytes()
                            assert after[48:84]==Path('/proc/sys/kernel/random/boot_id').read_bytes().strip()
                            assert int.from_bytes(after[24:32],'little')==2
                        if case=='retry':
                            previous=(component/'lifecycle').read_bytes()
                            again=subprocess.run([*runner,binary,action],env=env,capture_output=True,text=True,timeout=10)
                            assert again.returncode==0,(arch,again.stdout,again.stderr)
                            assert (component/'lifecycle').read_bytes()==previous
                    else:
                        assert [(tree/p[1:]).read_bytes() for p in PATHS]==before,(arch,case)
                        if case=='check':assert not (component/'lifecycle').exists() and not (component/'original-stock-44.bin').exists()
                    if case!='busy':assert not (folder/'install.lock').exists(),(arch,case,'operation lock leaked')
                    assert n.poll() is None or case=='native-missing'
                    assert a.poll() is None or case=='app-missing'
                    count+=1
                finally:
                    for process in processes:
                        if process.poll() is None:process.terminate()
                        process.wait(timeout=5)
    print(f'PASS: {count} actual host-stage cases (stock bootstrap={stock_bootstrap}) on host/ARM/sanitizers; app/native processes preserved, no UART or firmware action')
    return count
if __name__=='__main__':run()
