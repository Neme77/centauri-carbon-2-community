"""Package the current cumulative app with the unchanged qualified network runtime."""
import argparse
import hashlib
import io
import json
from pathlib import Path
import re
import tarfile
import zipfile

ROOT = Path(__file__).resolve().parent
sha = lambda data: hashlib.sha256(data).hexdigest()

def read_tar(data):
    files = {}
    with tarfile.open(fileobj=io.BytesIO(data), mode='r:gz') as archive:
        for entry in archive:
            if entry.isdir():
                continue
            if not entry.isfile() or entry.name.startswith('/') or '..' in Path(entry.name).parts or entry.name in files:
                raise ValueError('Unsafe archive entry: ' + entry.name)
            files[entry.name] = archive.extractfile(entry).read()
    for line in files['SHA256SUMS'].decode().splitlines():
        digest, name = line.split('  ', 1)
        if sha(files[name]) != digest:
            raise ValueError('Payload checksum mismatch: ' + name)
    return files

def package(app_zip, runtime, output):
    lock = json.loads((ROOT / 'runtime.json').read_text())
    network = runtime.read_bytes()
    if sha(network) != lock['sha256']:
        raise ValueError('Runtime differs from the physically qualified component')
    component = read_tar(network)
    if sha(component['control']) != lock['helper_sha256']:
        raise ValueError('Unqualified helper')
    with zipfile.ZipFile(app_zip) as archive:
        def member(suffix):
            names = [n for n in archive.namelist() if n.endswith('/' + suffix)]
            if len(names) != 1:
                raise ValueError('Missing or ambiguous app member: ' + suffix)
            return archive.read(names[0])
        payload, sources = member('cc2-control-payload.tar.gz'), member('cc2-control-sources.zip')
    app = read_tar(payload)
    info = json.loads(app['build-info.json'])
    if info.get('camera_component') != 'reap-r2+recovery-r2':
        raise ValueError('Cumulative camera component is required')
    version = info['version']
    if not re.fullmatch(r'[0-9]+\.[0-9]+\.[0-9]+', version):
        raise ValueError('Invalid version')
    stage = '/opt/usr/cc2-control-connections-' + version.replace('.', '-')
    files = {'cc2-control-payload.tar.gz': payload, 'cc2-connections.tar.gz': network,
             'cc2-control-sources.zip': sources}
    def script(name, text):
        if '\r' in text or '\ufeff' in text:
            raise ValueError('Non-Linux line endings in ' + name)
        files[name] = text.replace('/opt/usr/cc2-control-connections-r5', stage).encode()
    embedded = info.get('connections_runtime_sha256') == lock['sha256']
    script('run-install.sh', (ROOT / 'run-install.sh.in').read_text().replace('@APP_SHA@', sha(payload)).replace('@NETWORK_SHA@', sha(network)).replace('@NETWORK_EMBEDDED@', '1' if embedded else '0'))
    script('prepare-disk.sh', '''#!/bin/sh
set -eu
umask 077
[ "$(id -u)" = 0 ] || exit 1
stage=/opt/usr/cc2-control-connections-r5
[ ! -e "$stage" ] && [ ! -L "$stage" ] || { echo 'STOP: previous stage exists; preserve its logs';exit 1; }
free=$(df -k /opt/usr | awk 'NR==2 {print $4}')
case "$free" in ''|*[!0-9]*) exit 1;;esac
[ "$free" -ge 65536 ] || { echo 'STOP: less than 64 MiB free on /opt/usr';exit 1; }
mkdir "$stage"
echo 'Disk staging prepared'
''')
    script('verify-and-run.sh', '''#!/bin/sh
set -eu
umask 077
BASE=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd -P)
[ "$BASE" = /opt/usr/cc2-control-connections-r5 ] && [ ! -L "$BASE" ] || exit 1
[ "$(sha256sum "$BASE/run-install.sh" | awk '{print $1}')" = ''' + sha(files['run-install.sh']) + ''' ] || exit 1
exec sh "$BASE/run-install.sh"
''')
    ps = (ROOT / 'Install-CC2-Control.ps1.in').read_text()
    values = {'APP_SHA': sha(payload), 'NETWORK_SHA': sha(network), 'STAGE': stage,
              'RUNNER_SHA': sha(files['run-install.sh']), 'PREPARE_SHA': sha(files['prepare-disk.sh']),
              'VERIFY_SHA': sha(files['verify-and-run.sh'])}
    for key, value in values.items():
        ps = ps.replace('@' + key + '@', value)
    if re.search(r'@[A-Z_]+@', ps):
        raise ValueError('Unresolved installer token')
    files['Install-CC2-Control.ps1'] = ps.encode()
    files['Install-Windows.cmd'] = (ROOT / 'Install-Windows.cmd').read_bytes()
    script('install-cc2-control.sh', (ROOT / 'unix-install.sh.in').read_text())
    files['README.md'] = (ROOT / 'README.en.md').read_bytes()
    files['validation.json'] = (json.dumps({'version': version, 'runtime': lock,
        'app_payload_sha256': sha(payload), 'connections_payload_sha256': sha(network),
        'new_cumulative_physical_validation': False, 'display_interception_included': False}, indent=2) + '\n').encode()
    files['SHA256SUMS'] = ''.join(sha(data) + '  ' + name + '\n' for name, data in sorted(files.items())).encode()
    if output.exists():
        raise ValueError('Refuse to overwrite a delivered package')
    output.parent.mkdir(parents=True, exist_ok=True)
    with zipfile.ZipFile(output, 'w', zipfile.ZIP_DEFLATED) as archive:
        for name, data in sorted(files.items()):
            entry = zipfile.ZipInfo(output.stem + '/' + name, (2026, 1, 1, 0, 0, 0))
            entry.compress_type = zipfile.ZIP_DEFLATED
            entry.external_attr = (0o100755 if name.endswith(('.sh', '.cmd')) else 0o100644) << 16
            archive.writestr(entry, data)
    output.with_suffix(output.suffix + '.sha256').write_bytes((sha(output.read_bytes()) + '  ' + output.name + '\n').encode())
    print('Cumulative Connections package:', output)

if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--app-updater', type=Path, required=True)
    parser.add_argument('--runtime', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    package(args.app_updater, args.runtime, args.output)
