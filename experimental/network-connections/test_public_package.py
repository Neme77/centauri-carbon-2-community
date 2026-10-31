"""Exercise the actual public packager with real qualified inputs and refusal cases."""
from pathlib import Path
import importlib.util
import io
import json
import subprocess
import sys
import tarfile
import tempfile
import zipfile

root = Path(__file__).resolve().parent
spec = importlib.util.spec_from_file_location('public_package', root / 'package_cumulative.py')
module = importlib.util.module_from_spec(spec)
spec.loader.exec_module(module)
app, runtime = Path(sys.argv[1]), Path(sys.argv[2])
with tempfile.TemporaryDirectory(prefix='cc2-public-package-') as tmp:
    base = Path(tmp)
    output = base / 'Complete.zip'
    module.package(app, runtime, output)
    with zipfile.ZipFile(output) as archive:
        files = {n.split('/', 1)[1]: archive.read(n) for n in archive.namelist()}
    validation = json.loads(files['validation.json'])
    assert validation['new_cumulative_physical_validation'] is False
    assert validation['display_interception_included'] is False
    for line in files['SHA256SUMS'].decode().splitlines():
        digest, name = line.split('  ', 1)
        assert module.sha(files[name]) == digest
    ps = files['Install-CC2-Control.ps1'].decode()
    assert '@STAGE@' not in ps and '@APP_SHA@' not in ps
    for name in ('cc2-control-payload.tar.gz', 'cc2-connections.tar.gz', 'run-install.sh', 'prepare-disk.sh', 'verify-and-run.sh'):
        assert module.sha(files[name]) in ps, name
    assert '/tmp/cc2-control-update' not in ps
    for name in ('run-install.sh', 'prepare-disk.sh', 'verify-and-run.sh', 'install-cc2-control.sh'):
        assert b'\r' not in files[name] and not files[name].startswith(b'\xef\xbb\xbf')
        path = base / name
        path.write_bytes(files[name])
        for shell in (['dash'], ['busybox', 'sh']):
            subprocess.run([*shell, '-n', path], check=True, timeout=10)
    try:
        module.package(app, runtime, output)
    except ValueError:
        pass
    else:
        raise AssertionError('Delivered package was overwritten')
    bad = base / 'changed-runtime.tar.gz'
    bad.write_bytes(runtime.read_bytes() + b'changed')
    try:
        module.package(app, bad, base / 'Bad.zip')
    except ValueError:
        assert not (base / 'Bad.zip').exists()
    else:
        raise AssertionError('Changed runtime accepted')
    data = io.BytesIO()
    with tarfile.open(fileobj=data, mode='w:gz') as archive:
        entry = tarfile.TarInfo('../unsafe')
        entry.size = 1
        archive.addfile(entry, io.BytesIO(b'x'))
    try:
        module.read_tar(data.getvalue())
    except ValueError:
        pass
    else:
        raise AssertionError('Traversal archive accepted')
print('PASS: complete public package, five transport pins, Linux/BusyBox syntax, changed runtime, traversal and overwrite refusal; no SSH executed')
