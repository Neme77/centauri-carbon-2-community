"""Pinned root delegate; fake helper only, dash and BusyBox."""
from pathlib import Path
import hashlib
import os
import subprocess
import tempfile

HERE=Path(__file__).resolve().parent
def run():
    count=0
    with tempfile.TemporaryDirectory(prefix='cc2-root-gate-') as temporary:
        root=Path(temporary)
        for shell in [['dash'],['busybox','sh']]:
            for case in ['boot','prepare','extra-prepare','missing','changed','symlink','preload']:
                folder=root/f'{shell[0]}-{case}';folder.mkdir(mode=0o700)
                worker=folder/'worker';trace=folder/'trace'
                worker.write_text('#!/bin/sh\nprintf "%s\\n" "$#" "$@" > "$TRACE"\n');worker.chmod(0o700)
                digest=hashlib.sha256(worker.read_bytes()).hexdigest()
                text=(HERE/'startup-gate.sh').read_text()
                for old,new in {'WORKER=/opt/usr/cc2-rfid/startup-worker':f'WORKER={worker}',
                                'WORKER_SHA=UNCONFIGURED':f'WORKER_SHA={digest}'}.items():
                    assert text.count(old)==1;text=text.replace(old,new)
                gate=folder/'gate';gate.write_text(text)
                args=['printer cfg','-s','autosave cfg']
                if case in ['prepare','extra-prepare']:args=['--prepare-only']
                if case=='extra-prepare':args+=['unexpected']
                if case=='missing':worker.unlink()
                if case=='changed':worker.write_text('#!/bin/sh\nexit 0\n')
                if case=='symlink':
                    saved=folder/'saved';worker.rename(saved);worker.symlink_to(saved)
                env=dict(os.environ,TRACE=str(trace))
                if case=='preload':env['LD_PRELOAD']='/definitely-missing-canvas-fixture.so'
                result=subprocess.run([*shell,gate,*args],env=env,text=True,capture_output=True,timeout=10)
                okay=case in ['boot','prepare']
                assert (result.returncode==0)==okay,(shell,case,result.stdout,result.stderr)
                if okay:
                    expected=['1','--prepare-only'] if case=='prepare' else ['4','--boot',*args]
                    assert trace.read_text().splitlines()==expected
                else:assert not trace.exists()
                count+=1
    print(f'PASS: {count} pinned startup delegate cases; exact arguments, prepare-only and refusal paths')
    return count
if __name__=='__main__':run()
