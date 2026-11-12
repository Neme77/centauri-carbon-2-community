"""Private packaging integrity and streamed data layout; no binary execution."""
from pathlib import Path
import hashlib
import importlib.util
import io
import json
import re
import sys
import tarfile
import tempfile

APP=Path(__file__).resolve().parents[1]
LIFE=APP.parent/'experimental/canvas-rfid/lifecycle'
sys.path.insert(0,str(LIFE));sys.path.insert(0,str(APP/'installer'))
from publish_bundle import runtime
from rfid_component import functions,BEGIN,END
sha=lambda b:hashlib.sha256(b).hexdigest()

def run():
    image=bytearray(64);image[:6]=b'\x7fELF\x01\x01';image[18]=40;image=bytes(image)
    app_sha='a'*64;profile=b'private profile fixture\n'
    metadata={'protocol':1,'app_sha256':app_sha,'coordinator_sha256':sha(image),
              'capture_sha256':sha(image),'profile_sha256':sha(profile)}
    files={'app-integration.json':json.dumps(metadata).encode(),'coordinator':image,'capture':image,
           'recovery-profile':profile,'activate-on-ready':f'CC2-RFID-ACTIVATE-1\n{app_sha}\n{sha(profile)}\n'.encode()}
    modes={n:0o600 for n in files}
    data=runtime(files,modes,image)
    with tempfile.TemporaryDirectory(prefix='rfid-package-') as temporary:
        path=Path(temporary)/'runtime.tar.gz';path.write_bytes(data)
        code,tail=functions(path,app_sha)
        assert BEGIN not in code.splitlines() and tail.startswith('\n'+BEGIN+'\n') and tail.endswith(END+'\n')
        chunks=[]
        for line in tail.splitlines():
            if not line or line.startswith('#'):continue
            match=re.fullmatch(r"printf '((?:\\[0-7]{3})+)' \|\| exit 1",line);assert match,line
            encoded=match[1];chunks.append(bytes(int(encoded[i+1:i+4],8) for i in range(0,len(encoded),4)))
        assert b''.join(chunks)==data and all(len(c)<=4096 for c in chunks)
        assert sha(data) in code and sha(image) in code and 'rm -' not in code and 'stat ' not in code
        assert 'sed -n' in code and '| sh >' in code
        for case in ['app-mismatch','checksum','foreign-publisher','missing-activation','wrong-activation']:
            if case=='app-mismatch':bad=data;expected='b'*64
            else:
                expected=app_sha
                with tarfile.open(fileobj=io.BytesIO(data),mode='r:gz') as archive:
                    entries={m.name:archive.extractfile(m).read() for m in archive}
                if case=='checksum':entries['component/coordinator']+=b'changed'
                if case=='foreign-publisher':entries['publisher']=b'foreign binary'
                if case=='missing-activation':entries.pop('component/activate-on-ready')
                if case=='wrong-activation':entries['component/activate-on-ready']=b'foreign activation'
                if case!='checksum':entries['SHA256SUMS']=''.join(f'{sha(v)}  {n}\n' for n,v in sorted(entries.items()) if n!='SHA256SUMS').encode()
                buffer=io.BytesIO()
                with tarfile.open(fileobj=buffer,mode='w:gz') as archive:
                    for name,body in entries.items():
                        item=tarfile.TarInfo(name);item.size=len(body);archive.addfile(item,io.BytesIO(body))
                bad=buffer.getvalue()
            path.write_bytes(bad)
            try:functions(path,expected)
            except ValueError:pass
            else:raise AssertionError(case)
    print('PASS: exact streamed RFID archive, bounded octal commands, native publisher/app/artifact bindings and malformed/foreign activation refusal')

if __name__=='__main__':run()
