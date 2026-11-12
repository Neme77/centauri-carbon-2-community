"""Reproduce Windows newline behavior even when this regression runs on Linux."""
from pathlib import Path
import hashlib,os,tempfile
from prepare_payload import prepare

repo=Path(__file__).resolve().parents[2]
original=Path.write_text
def windows_default(self,data,encoding=None,errors=None,newline=None):
    if newline is None:
        return original(self,data.replace('\n','\r\n'),encoding=encoding,errors=errors,newline='')
    return original(self,data,encoding=encoding,errors=errors,newline=newline)

parent=repo if os.name=='nt' else Path(tempfile.gettempdir()).resolve()
with tempfile.TemporaryDirectory(prefix='cavity-lf-',dir=parent) as temporary:
    root=Path(temporary).resolve()
    assert root.parent==parent and root.name.startswith('cavity-lf-')
    module=root/'dummy.so';module.write_bytes(b'only a packaging fixture')
    try:
        Path.write_text=windows_default
        out=prepare(repo,module,root/'component')
    finally:Path.write_text=original
    for name in ['SHA256SUMS','profile.original.sha256','launcher.sh','reactor-entry.sh','activate.sh','manage.sh','profile.sh','thermal.awk']:
        data=(out/name).read_bytes()
        assert b'\r' not in data,name
        assert data.endswith(b'\n'),name
    for line in (out/'SHA256SUMS').read_bytes().split(b'\n'):
        if not line:continue
        digest,name=line.split(b'  ',1)
        assert Path(name.decode()).name==name.decode()
        assert hashlib.sha256((out/name.decode()).read_bytes()).hexdigest()==digest.decode()
print('PASS: explicit Linux LF in actual payload/checksums under forced Windows newline defaults; exact bytes and all payload hashes verified.')
