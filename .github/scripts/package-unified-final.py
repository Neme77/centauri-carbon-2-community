"""Repackage a hardware-tested ARM application only when all native inputs match."""
from pathlib import Path
import hashlib,io,json,shutil,subprocess,tarfile,zipfile
ROOT=Path(__file__).resolve().parents[2]
APP=Path('/tmp/r3-app/CC2-Control-1.2.21-Multiplatform-Update.zip')
BUILDER=Path('/tmp/r3-builder/CC2-Builder-V4.4.2-Control-1.2.21-Test-R3.zip')
def sha(data):return hashlib.sha256(data).hexdigest()
def unzip(path):
    with zipfile.ZipFile(path) as z:
        prefix=z.namelist()[0].split('/')[0]+'/'
        assert all(n.startswith(prefix) and '..' not in Path(n).parts for n in z.namelist())
        return {n[len(prefix):]:z.read(n) for n in z.namelist() if not n.endswith('/')}
def check(files,name):
    for line in files[name].decode().splitlines():
        digest,path=line.split('  ',1);assert sha(files[path])==digest,path
def stage():
    assert sha(APP.read_bytes())=='875237d84fec96e1080fa9964f5893236174a73e53b1e9667a65466c89bcb235'
    assert sha(BUILDER.read_bytes())=='f9edf1d6c2116b97e9b508aecc4993e77931325f7bbd846f601abe98ed3687bf'
    app=unzip(APP);check(app,'SHA256SUMS')
    with zipfile.ZipFile(io.BytesIO(app['cc2-control-sources.zip'])) as z:old={n:z.read(n) for n in z.namelist()}
    names=subprocess.check_output(['git','ls-files','-z','cc2-control'],cwd=ROOT).decode().split('\0')
    for path in filter(None,names):
        name=path.removeprefix('cc2-control/')
        if name.startswith(('web/','web-src/','docs/','tests/','firmware-integration/overlay/opt/usr/cc2-control/web/','installer/README')):continue
        assert name in old and (ROOT/path).read_bytes()==old[name],('native input changed; recompile instead',name)
    with tarfile.open(fileobj=io.BytesIO(app['cc2-control-payload.tar.gz']),mode='r:gz') as t:
        members=t.getmembers();assert all(m.isfile() and '..' not in Path(m.name).parts and not m.name.startswith('/') for m in members)
        payload={m.name:t.extractfile(m).read() for m in members}
    check(payload,'SHA256SUMS')
    assert sha(payload['cc2-control'])=='47c9b18cfa77b87c49b85fcda97b7e1e5fa4ef9040eba10d7b7e54a0ae1b5785'
    prepared=Path('/tmp/unified-prepared');prepared.mkdir()
    for name in ('cc2-control','cc2-update-worker','cc2-https-download','ca-bundle.pem','update-runner.sh','updater-trusted-key.json','THIRD_PARTY_NOTICES.txt'):
        (prepared/name).write_bytes(payload[name])
    shutil.copytree(ROOT/'cc2-control/web',prepared/'web')
    builder=unzip(BUILDER);check(builder,'SHA256SUMS.txt')
    import importlib.util
    spec=importlib.util.spec_from_file_location('camera_component',ROOT/'cc2-control/installer/camera_component.py')
    camera=importlib.util.module_from_spec(spec);spec.loader.exec_module(camera)
    runtime=ROOT/'builder/current/components/camera/runtime';runtime.mkdir(parents=True,exist_ok=True)
    for name,digest in camera.PINS.items():
        b=builder['builder/current/components/camera/runtime/'+name];assert sha(b)==digest
        (runtime/name).write_bytes(b)
    print('PASS: exact qualified R3 artifacts, unchanged native build inputs, same ARM binary, runtime pins and payload manifests')
if __name__=='__main__':stage()