"""Actual assembled bytes through the fixed-path helper; preparation only.

The native executable is inert text, never vendor code. Shared libraries and
scripts are hashed/copied as data. No boot dispatch, native process or UART runs.
"""
from pathlib import Path
import hashlib
import json
import os
import shutil
import struct
import subprocess
import tempfile

HERE=Path(__file__).resolve().parent
sha=lambda b:hashlib.sha256(b).hexdigest()

def put(path,data,mode=0o600):
    path.parent.mkdir(parents=True,exist_ok=True,mode=0o700)
    path.write_bytes(data);path.chmod(mode)

def run(assembly,captured,route):
    summary=json.loads((assembly/'assembly.json').read_text())
    report=json.loads((captured/'inspection.json').read_text())
    original={f['path']:f for f in report['files']}
    count=0
    with tempfile.TemporaryDirectory(prefix='cc2-actual-boot-bytes-') as temporary:
        root=Path(temporary)
        for arch,compiler,extra,runner in [('host','gcc',[],[]),('arm','arm-linux-gnueabihf-gcc',['-static','-D_TIME_BITS=32','-D_FILE_OFFSET_BITS=32'],['qemu-arm'])]:
            folder=root/arch;component=folder/'component';tree=folder/'tree';binary=root/(arch+'-prepare')
            subprocess.run([compiler,'-O2','-std=c11','-Wall','-Wextra','-Wpedantic','-Werror',
                '-D_POSIX_C_SOURCE=200809L',*extra,'-DSW_BASE='+json.dumps(str(component)),
                '-DSW_PREFIX='+json.dumps(str(tree)),HERE/'startup_worker.c','-o',binary],check=True,timeout=120)
            for case in ['all-old','half-new','all-new','foreign-last','missing-backup','public-plan']:
                if folder.exists():shutil.rmtree(folder)
                component.mkdir(mode=0o700,parents=True);tree.mkdir(mode=0o700)
                for name,mode in summary['component_modes'].items():put(component/name,(assembly/'component'/name).read_bytes(),mode)
                lines=[]
                for i,t in enumerate(summary['startup_targets']):
                    old=(captured/original[t['path']]['local_name']).read_bytes()
                    new=(component/f'payload/{i}').read_bytes()
                    assert sha(old)==t['before_sha256'] and sha(new)==t['after_sha256']
                    put(component/f'startup-tx/{i}.old',old);put(component/f'startup-tx/{i}.new',new)
                    is_new=case=='all-new' or (case=='half-new' and i<4)
                    put(tree/t['path'][1:],new if is_new else old,t['after_mode'] if is_new else t['before_mode'])
                    lines.append(f'{i} {t["before_mode"]:04o} {t["after_mode"]:04o} {t["before_sha256"]} {t["after_sha256"]} {tree/t["path"][1:]}\n')
                digest=sha(''.join(lines).encode()).encode()+b'\n'
                put(component/'startup-tx/ready',digest)
                if case=='all-new':put(component/'startup-tx/applied',digest)
                put(tree/'opt/bin/run_printer.sh',route.read_bytes(),0o755)
                native=tree/'opt/usr/cc2-reactor-runtime-v2/elegoo_printer';put(native,b'INERT_NATIVE_NOT_EXECUTED')
                # Rebind the test namespace and inert native identity. All
                # assembled startup payload/plan/guard/entry bytes stay exact.
                roles=[]
                for line in (component/'recovery-profile').read_text().splitlines():
                    role,h,path=line.split(' ')
                    if role=='native':path=str(native);h=sha(native.read_bytes())
                    else:path=str(component/Path(path).relative_to('/opt/usr/cc2-rfid'))
                    roles.append(f'{role} {h} {path}\n')
                profile=''.join(roles).encode();put(component/'recovery-profile',profile)
                payload=bytearray(128);payload[:14]=b'CC2-CANVAS-LC1';payload[16]=1
                struct.pack_into('<Q',payload,24,1);payload[32:48]=bytes(range(1,17))
                payload[48:84]=b'00000000-0000-0000-0000-000000000001';payload[84:116]=hashlib.sha256(profile).digest()
                put(component/'lifecycle',payload+sha(payload).encode())
                if case=='foreign-last':put(tree/summary['startup_targets'][-1]['path'][1:],b'foreign')
                if case=='missing-backup':(component/'startup-tx/7.old').unlink()
                if case=='public-plan':(component/'startup-plan').chmod(0o644)
                before=[(tree/t['path'][1:]).read_bytes() for t in summary['startup_targets']]
                result=subprocess.run([*runner,binary,'--prepare-only'],capture_output=True,text=True,timeout=15)
                okay=case in ['all-old','half-new','all-new']
                assert (result.returncode==0)==okay,(arch,case,result.stdout,result.stderr)
                after=[(tree/t['path'][1:]).read_bytes() for t in summary['startup_targets']]
                expected=[(component/f'payload/{i}').read_bytes() for i in range(8)] if okay else before
                assert after==expected,(arch,case)
                count+=1
    print(f'PASS: {count} host/ARM preparation cases using all eight exact assembled payloads; no native entry or UART run')
    return count
