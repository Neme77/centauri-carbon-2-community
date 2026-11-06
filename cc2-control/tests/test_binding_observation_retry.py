"""Actual generated shell wait: retry only after the original live guard passes."""
from pathlib import Path
import hashlib,io,json,os,subprocess,sys,tarfile,tempfile

sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'installer'))
from app_binding_component import functions


def run():
    sha=lambda b:hashlib.sha256(b).hexdigest()
    elf=bytearray(64);elf[:6]=b'\x7fELF\x01\x01';elf[18]=40;elf=bytes(elf)
    entries={'publisher':elf,'component/worker':elf,
        'component/recovery.json':json.dumps({'protocol':1,'app_sha256':'ab'*32,
            'worker_sha256':sha(elf),'plan_sha256':'cd'*32}).encode()}
    entries.update({'component/incoming/'+n:b'fixture' for n in ['coordinator','capture','app-integration.json','activate-on-ready']})
    entries['SHA256SUMS']=''.join(sha(b)+'  '+n+'\n' for n,b in sorted(entries.items())).encode()
    data=io.BytesIO()
    with tarfile.open(fileobj=data,mode='w:gz') as archive:
        for name,content in entries.items():
            item=tarfile.TarInfo(name);item.size=len(content);item.mode=(0o700 if name in
                ['publisher','component/worker','component/incoming/coordinator','component/incoming/capture'] else 0o600)
            archive.addfile(item,io.BytesIO(content))
    count=0
    with tempfile.TemporaryDirectory(prefix='cc2-observation-retry-') as temporary:
        base=Path(temporary);bundle=base/'bundle';bundle.write_bytes(data.getvalue())
        code,_,_=functions(bundle,'ab'*32)
        body=code[code.index('binding_rejected() {'):code.index('binding_unpack() {')]
        for shell in [['dash'],['busybox','sh']]:
            for case in ['ready','recovers','persistent','guard-refused','foreign-failure','guard-changed']:
                root=base/(shell[0]+'-'+case);root.mkdir();calls=root/'calls';observations=root/'observations'
                candidate=root/'candidate';candidate.write_text(f'''#!/bin/sh
n=0;[ ! -f '{calls}' ] || n=$(cat '{calls}');n=$((n+1));echo "$n" > '{calls}'
case '{case}' in
ready) exit 0;;
recovers) [ "$n" -lt 2 ] || exit 0;;
foreign-failure) echo 'STOP: profile_changed';exit 7;;
esac
echo 'STOP: fresh_idle_cold_observation_failed'
echo 'STOP: qualified_app_binding_action_refused; retain transaction and backups'
exit 7
''');candidate.chmod(0o700)
                guard=root/'guard';guard.write_text(f'#!/bin/sh\necho check >> "{observations}"\n'+('exit 1\n' if case=='guard-refused' else 'exit 0\n'));guard.chmod(0o700)
                expected=sha(guard.read_bytes())
                if case=='guard-changed':guard.write_text('#!/bin/sh\nexit 0\n')
                local=body.replace('/opt/usr/cc2-rfid/fresh-reader',str(guard)).replace(
                    '3dd6d8699169c800689e5ed2c047a2d3cc43386d3fdad59adc41b929111b045a',expected)
                # Only the two-second backoff is accelerated; no guard is bypassed.
                local=local.replace('        sleep 2\n','        :\n')
                script=root/'run.sh';script.write_text(f'#!/bin/sh\nBINDING_OBSERVATION=0\nBINDING_SOURCE="{root}"\n'+local+f'\nbinding_run "{candidate}"\n')
                result=subprocess.run([*shell,script],capture_output=True,text=True,timeout=15)
                assert (result.returncode==0)==(case in ['ready','recovers']),(shell,case,result.stdout,result.stderr)
                expected_calls=2 if case=='recovers' else 3 if case=='persistent' else 1
                assert int(calls.read_text())==expected_calls,(shell,case)
                guard_calls=len(observations.read_text().splitlines()) if observations.exists() else 0
                assert guard_calls==(1 if case in ['recovers','guard-refused'] else 2 if case=='persistent' else 0),(shell,case)
                assert len(list(root.glob('observation-*.log')))==expected_calls
                count+=1
    print(f'PASS: {count} generated observation-wait dash/BusyBox cases; original guard must pass before retry, limit3, foreign/tampered/refused guards stop and logs retained',flush=True)
    return count

if __name__=='__main__':run()
