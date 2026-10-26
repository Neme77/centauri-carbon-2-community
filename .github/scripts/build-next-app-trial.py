"""Build a version-only successor to exercise the panel on the maintainer's printer.

Not an official release. The sources differ only in their central VERSION;
the derived source archive is included, so the candidate is reproducible.
Never create or upload a production signing key in CI.
"""
import argparse
import io
import os
from pathlib import Path
import subprocess
import tempfile
import zipfile

repo=Path(__file__).resolve().parents[2]
source=repo/'builder/current/components/cc2-control/source/source.zip'
current=(repo/'cc2-control/VERSION').read_text().strip()
parser=argparse.ArgumentParser(description=__doc__)
parser.add_argument('--increments',type=int,choices=(1,2),default=1)
args=parser.parse_args()
parts=list(map(int,current.split('.')));parts[-1]+=args.increments
next_version='.'.join(map(str,parts))
output=repo/'output/panel-install-trial'
output.mkdir(parents=True,exist_ok=True)
with tempfile.TemporaryDirectory(prefix='cc2-panel-next-') as temporary:
    base=Path(temporary);control=base/'cc2-control';control.mkdir()
    archive_bytes=io.BytesIO()
    with zipfile.ZipFile(source) as original,zipfile.ZipFile(archive_bytes,'w',compression=zipfile.ZIP_DEFLATED,compresslevel=9) as derived:
        for member in original.infolist():
            name=member.filename
            assert not name.startswith('/') and '..' not in Path(name).parts
            data=(next_version+'\n').encode() if name=='VERSION' else original.read(member)
            derived.writestr(member,data)
            destination=control/name;destination.parent.mkdir(parents=True,exist_ok=True);destination.write_bytes(data)
    next_source=base/'next-source.zip';next_source.write_bytes(archive_bytes.getvalue())
    subprocess.run(['make','clean','all','EXTRA_CFLAGS=-Werror'],cwd=control,check=True)
    subprocess.run(['python3','tests/test_smoke.py','dist/cc2-control/cc2-control'],cwd=control,
        env=dict(os.environ,CC2_TEST_RUNNER='qemu-arm'),check=True)
    target=output/f'CC2-Control-{next_version}-Panel-Install-Trial.zip'
    subprocess.run(['python3','installer/package.py','--prepared','dist/cc2-control','--source',str(next_source),'--output',str(target)],cwd=control,check=True)
    (output/'TRIAL.txt').write_text(f'Bootstrap: {current}\nPanel installation candidate: {next_version}\n'
        'Same functionality, central VERSION incremented for a real signed update trial.\n'
        'Not an official release. Do not publish to the stable channel before printer qualification.\n')
print('PASS: ARM version-only successor built and smoke-tested for panel installation')
