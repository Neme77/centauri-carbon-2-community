#!/usr/bin/env python3
"""Package the current ARM build with the tracked updater and rollback scripts."""
import argparse
import gzip
import hashlib
import io
import json
from pathlib import Path
import struct
import tarfile
import zipfile

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[1]
VERSION = '1.1.31'

def package(prepared, scripts, source, output):
    binary = (prepared / 'cc2-control').read_bytes()
    if len(binary) < 20 or binary[:6] != b'\x7fELF\x01\x01' or struct.unpack_from('<H', binary, 18)[0] != 40:
        raise ValueError('Payload requires a 32-bit little-endian ARM ELF')
    binary_hash = hashlib.sha256(binary).hexdigest()
    files = {'cc2-control': binary}
    for name in ('start.sh', 'launch.sh', 'cc2-control.init'):
        data = (scripts / name).read_bytes().decode('utf-8-sig').replace('\r\n', '\n').replace('\r', '\n').encode()
        if not data.startswith(b'#!'): raise ValueError('Missing shebang: ' + name)
        files[name] = data
    if b'/opt/usr/cc2-control/launch.sh' not in files['cc2-control.init']:
        raise ValueError('Updater init must launch the persistent installation')
    for path in sorted((prepared / 'web').rglob('*')):
        if path.is_file(): files[path.relative_to(prepared).as_posix()] = path.read_bytes()
    if 'web/index.html' not in files or 'web/locales/en.json' not in files:
        raise ValueError('Prepared UI is incomplete')
    for name in ('install-on-printer.sh', 'restore-on-printer.sh'):
        text = (HERE / name).read_text().replace('@BINARY_SHA256@', binary_hash)
        files[name] = text.encode()
    files['build-info.json'] = (json.dumps({
        'version': VERSION, 'binary_sha256': binary_hash,
        'source_archive_sha256': hashlib.sha256(source.read_bytes()).hexdigest(),
        'change': 'first-run loopback HTTP serial discovery',
    }, indent=2) + '\n').encode()
    files['SHA256SUMS'] = ''.join(hashlib.sha256(data).hexdigest() + '  ' + name + '\n'
        for name, data in sorted(files.items())).encode()
    payload = io.BytesIO()
    with gzip.GzipFile(fileobj=payload, mode='wb', mtime=0) as compressed:
        with tarfile.open(fileobj=compressed, mode='w') as archive:
            for name, data in sorted(files.items()):
                entry = tarfile.TarInfo(name); entry.size = len(data)
                entry.mode = 0o755 if name == 'cc2-control' or name.endswith(('.sh', '.init')) else 0o644
                entry.uid = entry.gid = 0; entry.mtime = 0
                archive.addfile(entry, io.BytesIO(data))
    public = {name: (HERE / name).read_bytes() for name in
              ('Install-CC2-Control.ps1', 'Install-Windows.cmd', 'install-cc2-control.sh', 'README.md')}
    public[f'cc2-control-{VERSION}-payload.tar.gz'] = payload.getvalue()
    public['cc2-control-sources.zip'] = source.read_bytes()
    public['SHA256SUMS'] = ''.join(hashlib.sha256(data).hexdigest() + '  ' + name + '\n'
        for name, data in sorted(public.items())).encode()
    output.parent.mkdir(parents=True, exist_ok=True)
    with zipfile.ZipFile(output, 'w', zipfile.ZIP_DEFLATED) as archive:
        for name, data in sorted(public.items()):
            entry = zipfile.ZipInfo(output.stem + '/' + name, (2026, 1, 1, 0, 0, 0))
            entry.compress_type = zipfile.ZIP_DEFLATED
            entry.external_attr = (0o100755 if name.endswith(('.sh', '.cmd')) else 0o100644) << 16
            archive.writestr(entry, data)
    print('Updater:', output)
    print('ARM binary SHA256:', binary_hash)
    print('Updater SHA256:', hashlib.sha256(output.read_bytes()).hexdigest())

if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--prepared', type=Path, default=ROOT / 'builder/current/components/cc2-control/prepared')
    parser.add_argument('--scripts', type=Path, default=HERE.parent / 'scripts')
    parser.add_argument('--source', type=Path, default=ROOT / 'builder/current/components/cc2-control/source/source.zip')
    parser.add_argument('--output', type=Path, default=ROOT / 'output/CC2-Control-1.1.31-Serial-Discovery-Multiplatform-Update.zip')
    args = parser.parse_args()
    package(args.prepared, args.scripts, args.source, args.output)
