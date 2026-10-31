"""Recover only the three pinned CC2 camera binaries from a verified public updater.

The installer is parsed as data, never executed. No vendor binary is extracted.
"""
import hashlib
import io
from pathlib import Path
import re
import sys
import tarfile
import zipfile

repo = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(repo / 'cc2-control/installer'))
from camera_component import PINS

baseline, output = Path(sys.argv[1]), Path(sys.argv[2])
assert hashlib.sha256(baseline.read_bytes()).hexdigest() == '7253add5b4a0e7f183208bfd10a051d5072e449dd1c2f91c5f89576d8102f5ca'
with zipfile.ZipFile(baseline) as archive:
    names = [n for n in archive.namelist() if n.endswith('/cc2-control-payload.tar.gz')]
    assert len(names) == 1
    with tarfile.open(fileobj=io.BytesIO(archive.read(names[0])), mode='r:gz') as payload:
        script = payload.extractfile('install-on-printer.sh').read().decode()
body = script.split('camera_unpack() {\n', 1)[1].split('\n}\n', 1)[0]
chunks = re.findall(r"^    printf '((?:\\[0-7]{3})+)' (?:>|>>) \"\$CAMERA_SOURCE/payload.tar.gz\" \|\| return 1$", body, re.M)
assert chunks
data = bytes(int(value, 8) for chunk in chunks for value in re.findall(r'\\([0-7]{3})', chunk))
files = {}
with tarfile.open(fileobj=io.BytesIO(data), mode='r:gz') as archive:
    for name, expected in PINS.items():
        entry = archive.getmember(name)
        assert entry.isfile()
        files[name] = archive.extractfile(entry).read()
        assert hashlib.sha256(files[name]).hexdigest() == expected, name
output.mkdir(parents=True, exist_ok=False)
for name, content in files.items():
    (output / name).write_bytes(content)
print('PASS: three byte-exact CC2 camera runtime files recovered without executing the installer')
