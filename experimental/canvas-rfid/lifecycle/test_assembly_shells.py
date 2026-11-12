"""Generated shell syntax and isolated RFID qualification; inert helpers only."""
from pathlib import Path
import hashlib
import json
import os
import subprocess
import tempfile

HERE=Path(__file__).resolve().parent
def run(assembly):
    summary=json.loads((assembly/'assembly.json').read_text())
    component=assembly/'component'
    scripts=[p for p in component.iterdir() if p.suffix=='.sh']
    scripts += [component/f'payload/{i}' for i in (0,1,3,4,5)]
    syntax=0;cases=0
    activation=(component/'payload/5').read_text()
    start=activation.index('    RFID=')
    end=activation.index('    CAVITY=',start) if summary.get('requires_cavity') else activation.index('    case "$p" in "$REACTOR"',start)
    block=activation[start:end]
    with tempfile.TemporaryDirectory(prefix='cc2-assembled-shell-') as temporary:
        root=Path(temporary)
        for shell in [['dash'],['busybox','sh']]:
            for script in scripts:
                subprocess.run([*shell,'-n',script],check=True,capture_output=True,timeout=10);syntax+=1
            for case in ['core','cavity','network','missing-marker','wrong-marker','modified-library','missing-mapping','no-rfid','symlink-marker','symlink-library']:
                folder=root/f'{shell[0]}-{case}';folder.mkdir(mode=0o700)
                library=folder/'libcc2-rfid.so';library.write_bytes((component/'libcc2-rfid.so').read_bytes())
                marker=folder/'enabled';marker.write_bytes(b'CC2-RFID-NATIVE-R2\n')
                maps=folder/'maps';maps.write_text(str(library)+'\n')
                preload='/opt/usr/cc2-reactor-runtime-v2/libcc2-reactor-v2.so'
                if case in ['cavity','network']:preload+=':/opt/usr/cc2-cavity-floor/libcc2-cavity-floor-r1.so'
                if case=='network':preload='/opt/usr/cc2-network-manager/libcc2-network-compat-r1.so:'+preload
                expected=preload
                if case!='no-rfid':preload+=':'+str(library)
                if case=='missing-marker':marker.unlink()
                if case=='wrong-marker':marker.write_text('wrong\n')
                if case=='modified-library':library.write_bytes(library.read_bytes()+b'changed')
                if case=='missing-mapping':maps.write_text('')
                if case in ['symlink-marker','symlink-library']:
                    original=marker if case=='symlink-marker' else library
                    saved=folder/(original.name+'.saved');original.rename(saved);original.symlink_to(saved)
                trace=folder/'trace';script=folder/'qualify'
                text=block.replace('/opt/usr/cc2-rfid',str(folder)).replace('"/proc/$printer/maps"','"'+str(maps)+'"')
                script.write_text('#!/bin/sh\nset -eu\nfail(){ exit 1; }\nprinter=99\np=$PRELOADS\n'+text+'printf "%s" "$p" > "$TRACE"\n')
                result=subprocess.run([*shell,script],env=dict(os.environ,PRELOADS=preload,TRACE=str(trace)),capture_output=True,text=True,timeout=10)
                okay=case in ['core','cavity','network']
                assert (result.returncode==0)==okay,(shell,case,result.stdout,result.stderr)
                if okay:assert trace.read_text()==expected
                else:assert not trace.exists()
                cases+=1
            for case in ['ready','busy','changed','missing','wrong-action']:
                folder=root/f'{shell[0]}-guard-{case}';folder.mkdir(mode=0o700)
                control=folder/'control';trace=folder/'trace'
                control.write_text('#!/bin/sh\nprintf "%s\\n" "$#" "$@" > "$TRACE"\nexit '+('1' if case=='busy' else '0')+'\n');control.chmod(0o700)
                digest=hashlib.sha256(control.read_bytes()).hexdigest()
                text=(component/'fresh-guard.sh').read_text().replace('CONTROL=/opt/usr/cc2-network-manager/control','CONTROL='+str(control)).replace('CONTROL_SHA=2a4dd2dd8e23aa3446348973ba6b8fa90a1fda16fcd96626015f8f0b816e9d62','CONTROL_SHA='+digest)
                script=folder/'guard';script.write_text(text)
                if case=='changed':control.write_text('#!/bin/sh\nexit 0\n')
                if case=='missing':control.unlink()
                action='--install' if case=='wrong-action' else '--check'
                result=subprocess.run([*shell,script,action],env=dict(os.environ,TRACE=str(trace)),capture_output=True,text=True,timeout=10)
                assert (result.returncode==0)==(case=='ready'),(shell,case,result.stdout,result.stderr)
                if case in ['ready','busy']:assert trace.read_text().splitlines()==['2','--guard','8081']
                else:assert not trace.exists()
                cases+=1
    print(f'PASS: {syntax} generated shell syntax checks and {cases} isolated network/RFID/readiness-wrapper cases')
    return syntax,cases
