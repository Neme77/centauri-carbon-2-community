"""Validate the private package boundary without executing any payload."""
from pathlib import Path
import gzip, hashlib, io, json, sys, tarfile, tempfile

sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'installer'))
from app_binding_component import functions


def run():
    sha=lambda b:hashlib.sha256(b).hexdigest()
    elf=bytearray(64);elf[:6]=b'\x7fELF\x01\x01';elf[18]=40;elf=bytes(elf)
    app='ab'*32
    metadata={'protocol':1,'app_sha256':app,'worker_sha256':sha(elf),'plan_sha256':'cd'*32}
    original={'publisher':elf,'component/worker':elf,'component/recovery.json':json.dumps(metadata).encode()}
    original.update({'component/incoming/'+n:('new '+n).encode() for n in
        ['coordinator','capture','app-integration.json','activate-on-ready']})
    count=0
    with tempfile.TemporaryDirectory() as temporary:
        path=Path(temporary)/'runtime.tar.gz'
        for case in ['valid','wrong-app','missing','unexpected','duplicate','symlink','public-mode','owner',
                     'bad-worker','bad-publisher','bad-manifest','extra-key','duplicate-key','bad-protocol','wrong-worker']:
            files=dict(original);md=dict(metadata)
            if case=='missing':files.pop('component/incoming/capture')
            if case=='unexpected':files['../outside']=b'foreign'
            if case=='bad-worker':files['component/worker']=b'not ELF'
            if case=='bad-publisher':files['publisher']=b'not ELF'
            if case=='extra-key':md['extra']=True
            if case=='bad-protocol':md['protocol']=True
            if case=='wrong-worker':md['worker_sha256']='ef'*32
            files['component/recovery.json']=json.dumps(md).encode()
            if case=='duplicate-key':files['component/recovery.json']=json.dumps(md).replace('"protocol": 1','"protocol": 1, "protocol": 1').encode()
            manifest=''.join(sha(b)+'  '+n+'\n' for n,b in sorted(files.items())).encode()
            files['SHA256SUMS']=manifest if case!='bad-manifest' else b'00  publisher\n'
            buffer=io.BytesIO()
            with gzip.GzipFile(fileobj=buffer,mode='wb',mtime=0) as compressed:
                with tarfile.open(fileobj=compressed,mode='w') as archive:
                    for name,data in files.items():
                        item=tarfile.TarInfo(name);item.size=len(data)
                        item.mode=0o700 if name in ['publisher','component/worker','component/incoming/coordinator','component/incoming/capture'] else 0o600
                        if case=='public-mode':item.mode=0o777
                        if case=='owner':item.uid=1000
                        if case=='symlink' and name=='publisher':item.type=tarfile.SYMTYPE;item.linkname='/outside';item.size=0
                        archive.addfile(item,io.BytesIO(data))
                        if case=='duplicate' and name=='publisher':archive.addfile(item,io.BytesIO(data))
            path.write_bytes(buffer.getvalue())
            try:code,tail,restore=functions(path,'ef'*32 if case=='wrong-app' else app)
            except ValueError:
                assert case!='valid',case
            else:
                assert case=='valid',case
                assert '--prepare' in code and '--commit' in code and '--rollback' in restore
                assert app in code and 'CC2_APP_BINDING_DATA_BEGIN' in tail
            count+=1
    print(f'PASS: {count} app-binding package acceptance/refusal cases; no payload executed',flush=True)
    return count

if __name__=='__main__':run()
