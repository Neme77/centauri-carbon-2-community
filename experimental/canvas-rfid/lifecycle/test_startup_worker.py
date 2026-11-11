"""Actual fixed-path boot worker with temporary files and inert shell entries."""
from pathlib import Path
import fcntl
import hashlib
import json
import os
import shutil
import struct
import subprocess
import tempfile

HERE=Path(__file__).resolve().parent
sha=lambda b:hashlib.sha256(b).hexdigest()
PATHS=['/etc/init.d/cc2-reactor-launch','/opt/usr/cc2-reactor-runtime-v2/launcher.sh',
       '/opt/usr/cc2-reactor-runtime-v2/SHA256SUMS','/opt/usr/cc2-network-manager/profile.sh',
       '/opt/usr/cc2-network-manager/reactor-entry.sh','/opt/usr/cc2-network-manager/activate.sh',
       '/opt/usr/cc2-network-manager/SHA256SUMS','/opt/usr/cc2-network-manager/startup.sha256']
CASES=['prepare-old','boot-mixed','active-new','active-mixed','backup-bad','foreign-target',
       'plan-changed','wrong-role','journal-missing','locked','unknown-action','public-plan',
       'run-changed','payload-missing','extra-record','duplicate-index','remove-mixed',
       'disabled-old','staged-new-no-receipt','dispatch-changed','missing-transaction']

def put(path,data,mode=0o600):
    path.parent.mkdir(mode=0o700,parents=True,exist_ok=True)
    path.write_bytes(data);path.chmod(mode)

def journal(component,phase,profile):
    data=bytearray(128);data[:14]=b'CC2-CANVAS-LC1';data[16]=phase
    struct.pack_into('<Q',data,24,1);data[32:48]=bytes(range(1,17))
    data[48:84]=b'00000000-0000-0000-0000-000000000001'
    data[84:116]=hashlib.sha256(profile).digest()
    put(component/'lifecycle',data+sha(data).encode())

def fixture(folder,case):
    component=folder/'component';tree=folder/'tree';trace=folder/'trace'
    component.mkdir(mode=0o700,parents=True);tree.mkdir(mode=0o700)
    dispatch=b'#!/bin/sh\nprintf "DISPATCH:%s\\n" "$*" > "$TRACE"\npwd >> "$TRACE"\nprintf "%s\\n" "$#" "$@" >> "$TRACE"\n'
    stock=b'#!/bin/sh\nprintf "STOCK:%s\\n" "$*" > "$TRACE"\n'
    old=[b'#!/bin/sh\nprintf "OLD:%s\\n" "$*" > "$TRACE"\n']+[f'old-{i}'.encode() for i in range(1,8)]
    new=[f'new-{i}'.encode() for i in range(8)]
    modes=[0o755,0o700,0o600,0o700,0o700,0o700,0o644,0o600]
    rows=[];tx_rows=[]
    for i,path in enumerate(PATHS):
        put(component/f'payload/{i}',new[i]);put(component/f'startup-tx/{i}.old',old[i]);put(component/f'startup-tx/{i}.new',new[i])
        put(tree/path[1:],old[i],modes[i])
        rows.append(f'{i} {modes[i]:04o} {modes[i]:04o} {sha(old[i])} {sha(new[i])}\n')
        tx_rows.append(rows[-1][:-1]+f' {tree/path[1:]}\n')
    tx_plan=sha(''.join(tx_rows).encode()).encode()+b'\n'
    put(component/'startup-tx/ready',tx_plan)
    put(component/'boot-dispatch.sh',dispatch,0o700);put(component/'stock-entry.sh',stock,0o700)
    plan=b'CC2-STARTUP-PLAN-1\n'+f'dispatch {sha(dispatch)}\nstock-entry {sha(stock)}\n'.encode()+''.join(rows).encode()
    phase=1;all_new=case in ['active-new','staged-new-no-receipt','remove-mixed','disabled-old']
    if all_new:
        for i,path in enumerate(PATHS):put(tree/path[1:],new[i],modes[i])
        if case!='staged-new-no-receipt':put(component/'startup-tx/applied',tx_plan)
    if case in ['boot-mixed','active-mixed']:put(tree/PATHS[0][1:],new[0],modes[0])
    if case in ['active-new','active-mixed']:phase=5
    if case=='remove-mixed':
        phase=9;put(tree/PATHS[-1][1:],old[-1],modes[-1])
    if case=='disabled-old':
        phase=10
        for i,path in enumerate(PATHS):put(tree/path[1:],old[i],modes[i])
        put(component/'startup-tx/restored',tx_plan)
    run=b'inert canonical run fixture\n';put(tree/'opt/bin/run_printer.sh',run,0o755)
    native=tree/'opt/usr/cc2-reactor-runtime-v2/elegoo_printer';put(native,b'not executable vendor data')
    roles=[('bootstrap',component/'startup-plan'),('native',native),('guard',component/'libcc2-rfid.so'),
           ('entry',component/'guarded-entry.sh'),('recover',component/'recover-worker'),('marker',component/'enabled')]
    for name,path in roles[2:]:put(path,('inert '+name).encode())
    if case=='extra-record':plan+=b'extra\n'
    if case=='duplicate-index':plan=plan.replace(rows[1].encode(),rows[1].replace('1 ', '0 ',1).encode())
    put(component/'startup-plan',plan)
    profile=''.join(f'{name} {sha(path.read_bytes())} {path}\n' for name,path in roles).encode()
    if case=='wrong-role':profile=profile.replace(b'bootstrap ',b'wrongrole ',1)
    put(component/'recovery-profile',profile);journal(component,phase,profile)
    if case=='plan-changed':put(component/'startup-plan',plan+b'changed\n')
    if case=='public-plan':(component/'startup-plan').chmod(0o644)
    if case=='payload-missing':(component/'payload/7').unlink()
    if case=='backup-bad':put(component/'startup-tx/7.old',b'bad')
    if case=='foreign-target':put(tree/PATHS[7][1:],b'foreign')
    if case=='journal-missing':(component/'lifecycle').unlink()
    if case=='run-changed':put(tree/'opt/bin/run_printer.sh',b'changed',0o755)
    if case=='dispatch-changed':put(component/'boot-dispatch.sh',b'#!/bin/sh\nexit 0\n',0o700)
    if case=='missing-transaction':shutil.rmtree(component/'startup-tx')
    return component,tree,trace,old,new,modes

def run():
    count=0
    flags=['-O2','-std=c11','-Wall','-Wextra','-Wpedantic','-Werror','-D_POSIX_C_SOURCE=200809L']
    with tempfile.TemporaryDirectory(prefix='cc2-fixed-startup-') as temporary:
        root=Path(temporary)
        for arch,compiler,extra,runner in [
            ('host','gcc',[],[]),
            ('arm','arm-linux-gnueabihf-gcc',['-static','-D_TIME_BITS=32','-D_FILE_OFFSET_BITS=32'],['qemu-arm']),
            ('sanitizers','gcc',['-fsanitize=address,undefined','-fno-sanitize-recover=all','-fno-pie','-no-pie'],[]),
        ]:
            folder=root/arch;binary=root/(arch+'-worker')
            subprocess.run([compiler,*flags,*extra,'-DSW_BASE='+json.dumps(str(folder/'component')),
                '-DSW_PREFIX='+json.dumps(str(folder/'tree')),
                '-DSW_RUN_SHA='+json.dumps(sha(b'inert canonical run fixture\n')),
                HERE/'startup_worker.c','-o',binary],check=True,timeout=120)
            for case in CASES:
                if folder.exists():shutil.rmtree(folder)
                component,tree,trace,old,new,modes=fixture(folder,case)
                held=None
                if case=='locked':
                    held=(component/'lifecycle.lock').open('wb');fcntl.flock(held,fcntl.LOCK_EX)
                before=[(tree/p[1:]).read_bytes() for p in PATHS]
                args=['--prepare-only'] if case=='prepare-old' else ['--boot','printer cfg','-s','autosave cfg']
                if case=='unknown-action':args=['--install']
                result=subprocess.run([*runner,binary,*args],env=dict(os.environ,TRACE=str(trace)),cwd=root,text=True,capture_output=True,timeout=15)
                if held:held.close()
                okay=case in ['prepare-old','boot-mixed','active-new','remove-mixed','disabled-old','staged-new-no-receipt']
                assert (result.returncode==0)==okay,(arch,case,result.returncode,result.stdout,result.stderr)
                if okay:
                    expected=old if case in ['remove-mixed','disabled-old'] else new
                    assert [(tree/p[1:]).read_bytes() for p in PATHS]==expected,(arch,case)
                    assert [(tree/p[1:]).stat().st_mode&0o777 for p in PATHS]==modes
                    if case=='prepare-old':assert not trace.exists()
                    else:
                        lines=trace.read_text().splitlines();kind='OLD' if case in ['remove-mixed','disabled-old'] else 'DISPATCH'
                        assert lines[0]==kind+':printer cfg -s autosave cfg',(case,lines)
                        if kind=='DISPATCH':
                            assert lines[1]==str(root)
                            assert lines[2:]==['3','printer cfg','-s','autosave cfg']
                else:
                    assert not trace.exists(),(arch,case)
                    assert [(tree/p[1:]).read_bytes() for p in PATHS]==before,(arch,case,'mutated before refusal')
                count+=1
    print(f'PASS: {count} actual fixed-path startup-worker cases on host/ARM/sanitizers; no vendor executable, process stop or serial access')
    return count

if __name__=='__main__':run()
