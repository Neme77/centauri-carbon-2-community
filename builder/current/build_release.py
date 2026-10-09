#!/usr/bin/env python3
"""Build matching standalone and firmware candidates from the pinned sources."""
import argparse
import hashlib
import io
from pathlib import Path
import subprocess
import tarfile
import zipfile

HERE=Path(__file__).resolve().parent
REPO=HERE.parents[1]

def run(args,cwd=None):
    subprocess.run([str(a) for a in args],cwd=cwd,check=True)

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--mode',choices=('stock','community'),default='stock')
    parser.add_argument('--updater-only',action='store_true')
    args=parser.parse_args()
    libco=REPO/'experimental/combined-test/reactor/vendor-link/libcolib.so'
    if not libco.is_file() or hashlib.sha256(libco.read_bytes()).hexdigest()!='14288a4a73c339b039dd89597618ebb05702f90d668b76f74019d5d15ce67403':
        raise RuntimeError('Missing or incompatible native libco; use Build-Release.ps1')
    run(['python3',REPO/'experimental/combined-test/build.py'])
    if args.updater_only:
        return
    run(['python3',HERE/'prepare.py'])
    version=(REPO/'cc2-control/VERSION').read_text().strip()
    updater=REPO/'output'/('CC2-Control-'+version+'-Callback-O0-Update.zip')
    with zipfile.ZipFile(updater) as archive:
        payload=archive.read(updater.stem+'/combined-payload.tar.gz')
    with tarfile.open(fileobj=io.BytesIO(payload),mode='r:gz') as archive:
        for name,firmware_path in [('cc2-control',HERE/'components/cc2-control/prepared/cc2-control'),
            ('reactor/libcc2-reactor-v2.so',HERE/'components/reactor/prepared/libcc2-reactor-v2.so')]:
            if archive.extractfile(name).read()!=firmware_path.read_bytes():
                raise RuntimeError('Standalone/firmware component mismatch: '+name)
    run(['python3',HERE/'launch_helpers/run_build.py','--root',HERE,'--mode',args.mode,'--check-key'])
    run(['python3',HERE/'launch_helpers/run_build.py','--root',HERE,'--mode',args.mode,'--preflight'])
    run(['python3',HERE/'launch_helpers/run_build.py','--root',HERE,'--mode',args.mode])
    print('RELEASE BUILD PASS: retain hashes and validate this firmware on hardware before publishing.')

if __name__=='__main__':
    main()
