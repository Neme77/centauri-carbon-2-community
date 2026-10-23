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
import sys

sys.path.insert(0, str(Path(__file__).resolve().parent))
from camera_component import functions as camera_functions, RESTORE as CAMERA_RESTORE

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[1]
VERSION = (HERE.parent / 'VERSION').read_text().strip()
FIRMWARE_VERSION = (HERE.parent / 'FIRMWARE_VERSION').read_text().strip()
COMPATIBILITY = json.loads((HERE / 'compatibility.json').read_text())
if hashlib.sha256((HERE.parent / 'FIRMWARE_VERSION').read_bytes()).hexdigest() != COMPATIBILITY['firmware_version_file_sha256']:
    raise ValueError('Updater firmware compatibility record does not match FIRMWARE_VERSION')

def package(prepared, scripts, source, output, camera_runtime=None):
    binary = (prepared / 'cc2-control').read_bytes()
    if len(binary) < 20 or binary[:6] != b'\x7fELF\x01\x01' or struct.unpack_from('<H', binary, 18)[0] != 40:
        raise ValueError('Payload requires a 32-bit little-endian ARM ELF')
    binary_hash = hashlib.sha256(binary).hexdigest()
    files = {'cc2-control': binary}
    worker = (prepared / 'cc2-update-worker').read_bytes()
    if len(worker) < 20 or worker[:6] != b'\x7fELF\x01\x01' or struct.unpack_from('<H', worker, 18)[0] != 40:
        raise ValueError('Update verifier requires a 32-bit little-endian ARM ELF')
    files['cc2-update-worker'] = worker
    downloader = (prepared / 'cc2-https-download').read_bytes()
    if len(downloader) < 20 or downloader[:6] != b'\x7fELF\x01\x01' or struct.unpack_from('<H', downloader, 18)[0] != 40:
        raise ValueError('HTTPS helper requires a 32-bit ARM executable')
    files['cc2-https-download'] = downloader
    files['ca-bundle.pem'] = (prepared / 'ca-bundle.pem').read_bytes()
    for name in ('update-runner.sh', 'updater-trusted-key.json', 'THIRD_PARTY_NOTICES.txt'):
        files[name] = (prepared / name).read_bytes()
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
        text = (HERE / name).read_text().replace('@BINARY_SHA256@', binary_hash).replace('@VERSION@', VERSION).replace('@FIRMWARE_VERSION@', FIRMWARE_VERSION).replace('@PRINTER_SHA256@', COMPATIBILITY['printer_sha256']).replace('@DAEMON_SHA256@', COMPATIBILITY['daemon_sha256'])
        text = text.replace('@CAMERA_RESTORE_FUNCTION@', CAMERA_RESTORE)
        text = text.replace('@CAMERA_COMPONENT_FUNCTIONS@', camera_functions(HERE / 'camera', camera_runtime) if camera_runtime else '')
        text = text.replace('@CAMERA_COMPONENT_PREFLIGHT@', 'camera_unpack\nsh "$CAMERA_SOURCE/manage.sh" check' if camera_runtime else '')
        files[name] = text.encode()
    files['build-info.json'] = (json.dumps({
        'version': VERSION, 'binary_sha256': binary_hash,
        'source_archive_sha256': hashlib.sha256(source.read_bytes()).hexdigest(),
        'change': 'CC2 Control application updater',
        'camera_component': 'reap-r2+recovery-r2' if camera_runtime else None,
        'requires_full_printer_reboot': bool(camera_runtime),
        'required_community_firmware': FIRMWARE_VERSION,
        'compatibility': COMPATIBILITY,
    }, indent=2) + '\n').encode()
    files['SHA256SUMS'] = ''.join(hashlib.sha256(data).hexdigest() + '  ' + name + '\n'
        for name, data in sorted(files.items())).encode()
    payload = io.BytesIO()
    with gzip.GzipFile(fileobj=payload, mode='wb', mtime=0) as compressed:
        with tarfile.open(fileobj=compressed, mode='w') as archive:
            for name, data in sorted(files.items()):
                entry = tarfile.TarInfo(name); entry.size = len(data)
                entry.mode = 0o755 if name in ('cc2-control', 'cc2-update-worker', 'cc2-https-download') or name.endswith(('.sh', '.init')) else 0o644
                entry.uid = entry.gid = 0; entry.mtime = 0
                archive.addfile(entry, io.BytesIO(data))
    public = {name: (HERE / name).read_bytes() for name in
              ('Install-CC2-Control.ps1', 'Install-Windows.cmd', 'install-cc2-control.sh', 'README.md')}
    note_names = ['README-TEST.md', 'README-TEST.it.md']
    if camera_runtime is None:
        note_names += ['README-EXPERIMENTAL.md', 'README-EXPERIMENTAL.it.md']
    for name in note_names:
        trial_notes = HERE / name
        if trial_notes.is_file(): public[name] = trial_notes.read_bytes()
    public['cc2-control-payload.tar.gz'] = payload.getvalue()
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
    output.with_suffix(output.suffix + '.sha256').write_text(hashlib.sha256(output.read_bytes()).hexdigest() + '  ' + output.name + '\n')
    print('Updater SHA256:', hashlib.sha256(output.read_bytes()).hexdigest())

if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--prepared', type=Path, default=ROOT / 'builder/current/components/cc2-control/prepared')
    parser.add_argument('--scripts', type=Path, default=HERE.parent / 'scripts')
    parser.add_argument('--source', type=Path, default=ROOT / 'builder/current/components/cc2-control/source/source.zip')
    parser.add_argument('--output', type=Path, default=ROOT / f'output/CC2-Control-{VERSION}-Multiplatform-Update.zip')
    parser.add_argument('--camera-runtime', type=Path, help='Include the three fingerprinted R2 camera runtime files')
    parser.add_argument('--release', action='store_true', help='Require the cumulative camera component for a public release')
    parser.add_argument('--application-only-trial', action='store_true', help='Explicitly build a development-only app package without the cumulative camera component')
    args = parser.parse_args()
    if args.release and args.application_only_trial:
        parser.error('--release and --application-only-trial are mutually exclusive')
    if args.camera_runtime is None and (args.release or not args.application_only_trial):
        parser.error('Public cumulative releases require --camera-runtime, including for users who skipped earlier updates')
    package(args.prepared, args.scripts, args.source, args.output, args.camera_runtime)
