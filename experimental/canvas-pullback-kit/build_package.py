#!/usr/bin/env python3
"""Build a source-only pilot ZIP from an explicit allowlist; never include vendor inputs."""
import argparse
import hashlib
from pathlib import Path
import zipfile
FILES=['manage.py','patch_pullback.py','policy.py','snapshot.sh','transaction.sh','start.py','Start-Windows.cmd','Start-Linux-macOS.sh','README.en.md','README.it.md','test_kit.py','build_package.py']
def build(output):
    root=Path(__file__).resolve().parent
    files={name:(root/name).read_bytes() for name in FILES}
    files['LICENSE']=(root.parents[1]/'LICENSE').read_bytes()
    files['SHA256SUMS.txt']=''.join(hashlib.sha256(data).hexdigest()+'  '+name+'\n' for name,data in sorted(files.items())).encode()
    output=Path(output);output.parent.mkdir(parents=True,exist_ok=True)
    with zipfile.ZipFile(output,'x',compression=zipfile.ZIP_DEFLATED) as z:
        for name,data in sorted(files.items()):
            info=zipfile.ZipInfo('CC2-Canvas-Pullback-Experimental-R4/'+name,(2026,10,26,0,0,0))
            info.compress_type=zipfile.ZIP_DEFLATED;info.external_attr=0o100644<<16;z.writestr(info,data)
    with zipfile.ZipFile(output) as z:
        assert z.testzip() is None
        assert len(z.namelist())==len(files)
    sha=hashlib.sha256(output.read_bytes()).hexdigest()
    output.with_suffix(output.suffix+'.sha256').write_text(sha+'  '+output.name+'\n',encoding='utf-8')
    print(output.resolve());print('SHA256 '+sha)
if __name__=='__main__':
    parser=argparse.ArgumentParser();parser.add_argument('output');build(parser.parse_args().output)
