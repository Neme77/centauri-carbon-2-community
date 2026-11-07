"""Public release admission, separate from runtime/device safety checks."""
from pathlib import Path
import hashlib
import io
import json
import sys
import tarfile
import tempfile

ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'installer'))
from native_release import validate_release


class Blob:
    def __init__(self,data):self.data=data
    def read_bytes(self):return self.data


sha=lambda b:hashlib.sha256(b).hexdigest()
with tempfile.TemporaryDirectory() as temp:
    folder=Path(temp)
    app=b'not an actual app; fixture only'
    profile=b'inert profile'
    component={'coordinator':b'inert coordinator','capture':b'inert capture','recovery-profile':profile}
    metadata={'protocol':1,'app_sha256':sha(app),'profile_sha256':sha(profile),
              'coordinator_sha256':sha(component['coordinator']),'capture_sha256':sha(component['capture'])}
    component['app-integration.json']=json.dumps(metadata).encode()
    component['activate-on-ready']=f'CC2-RFID-ACTIVATE-1\n{sha(app)}\n{sha(profile)}\n'.encode()
    inner={'component/'+n:b for n,b in component.items()}
    inner['publisher']=b'inert publisher'
    inner['SHA256SUMS']=''.join(f'{sha(b)}  {n}\n' for n,b in sorted(inner.items())).encode()
    def archive(files):
        out=io.BytesIO()
        with tarfile.open(fileobj=out,mode='w:gz') as tar:
            for n,b in files.items():
                item=tarfile.TarInfo(n);item.mode=0o700 if n in ['publisher','component/coordinator','component/capture'] else 0o600
                item.size=len(b);tar.addfile(item,io.BytesIO(b))
        return out.getvalue()
    runtime=archive({'delivery.json':b'{}','rfid.tar.gz':archive(inner),'binding.tar.gz':b'inert','cavity.tar.gz':b'inert'})
    policy={'protocol':1,'version_sha256':sha((ROOT/'VERSION').read_text().strip().encode()),'community_base':'4.4.3','physical_flash_readback':False,
            'app_sha256':sha(app),'native_runtime_sha256':sha(runtime),'recovery_profile_sha256':sha(profile)}
    path=folder/'policy.json'
    path.write_text(json.dumps(policy))
    assert validate_release(Blob(runtime),app,'1.2.26',path)==policy
    for key,value in [('protocol',True),('version_sha256','0'*64),('community_base','4.3'),
                      ('physical_flash_readback',True),('app_sha256','0'*64),
                      ('native_runtime_sha256','0'*64),('recovery_profile_sha256','0'*64)]:
        changed=dict(policy);changed[key]=value;path.write_text(json.dumps(changed))
        try:validate_release(Blob(runtime),app,'1.2.26',path)
        except ValueError:pass
        else:raise AssertionError(key)
    path.write_text(json.dumps(policy))
    for blob,binary in [(runtime+b'changed',app),(runtime,app+b'changed')]:
        try:validate_release(Blob(blob),binary,'1.2.26',path)
        except ValueError:pass
        else:raise AssertionError('changed artifact admitted')
    path.write_text('{"protocol":1,"protocol":1}')
    try:validate_release(Blob(runtime),app,'1.2.26',path)
    except ValueError:pass
    else:raise AssertionError('duplicate policy admitted')
print('PASS: 11 public native admission cases; accepted bytes only, version/base/profile mismatch and modified artifacts refused')
