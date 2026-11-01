"""Rebuild the optional USB runtime without changing the published base inputs.

The output is a preview candidate, never an automatic-update catalog entry.
"""
from pathlib import Path
import gzip
import hashlib
import io
import json
import re
import subprocess
import sys
import tarfile

source = Path(__file__).resolve().parent
repo = source.parents[1]
app = repo / 'cc2-control'
base, probe, output = map(lambda p: Path(p).resolve(), sys.argv[1:])
output.mkdir(parents=True, exist_ok=False)
sha = lambda data: hashlib.sha256(data).hexdigest()
assert sha(base.read_bytes()) == '95db87b35dcf6799d5d57ec8ad046455c97e5cb297cc07ea4a2eeda90fb367de'
files = {}
with tarfile.open(base) as archive:
    for entry in archive:
        if entry.isdir():
            continue
        assert entry.isfile() and not entry.name.startswith('/') and '..' not in Path(entry.name).parts
        assert entry.name not in files
        files[entry.name] = (archive.extractfile(entry).read(), entry.mode)
for line in files['SHA256SUMS'][0].decode().splitlines():
    digest, name = line.split('  ', 1)
    assert sha(files[name][0]) == digest, name
legacy = output / 'legacy-probe.ko'
legacy.write_bytes(files['cc2_ue300_abi_probe.ko'][0])
def symbols(path):
    text = subprocess.check_output(['arm-linux-gnueabihf-nm', '-u', str(path)], text=True)
    return sorted({line.split()[-1] for line in text.splitlines()})
assert symbols(legacy) == symbols(probe), 'new probe imports differ from R1'
legacy.unlink()
probe_sha = sha(probe.read_bytes())
pins = source / 'profile-pins.h'
text, count = re.subn(r'(#define NG_CC2_UE300_ABI_PROBE_3000_KO_SHA\s*\\\s*)"[a-f0-9]{64}"',
                     lambda m: m[1] + '"' + probe_sha + '"', pins.read_text())
assert count == 1
pins.write_text(text.replace('/* Replaced by the reproducible probe build before packaging/publication. */\n', ''))
(app / 'src/connections/profile-pins.h').write_bytes(pins.read_bytes())
helper = output / 'control'
subprocess.run(['arm-linux-gnueabihf-gcc', '-std=c11', '-Os', '-Wall', '-Wextra', '-Wpedantic', '-Werror',
                '-static', '-s', source / 'manager.c', '-lm', '-o', helper], check=True)
status = json.loads(subprocess.check_output(['qemu-arm', str(helper), '--status'], text=True))
assert status['automatic_selection'] is False and status['usb']['approved'] is False
helper_sha = sha(helper.read_bytes())
files['control'] = (helper.read_bytes(), 0o700)
files[probe.name] = (probe.read_bytes(), 0o644)
for name in list(files):
    if name.startswith('source/connections/'):
        del files[name]
for path in sorted(source.rglob('*')):
    if not path.is_file() or set(path.relative_to(source).parts) & {'__pycache__'}:
        continue
    relative = path.relative_to(source).as_posix()
    if relative == 'runtime.json' or relative.endswith('.pyc'):
        continue  # Runtime's own hash cannot be embedded in itself.
    files['source/connections/' + relative] = (path.read_bytes(), 0o644)
    if path.parent == source and path.name.endswith('.sh'):
        files[path.name] = (path.read_bytes(), 0o700)
report = {
    'purpose': 'RTL8153 revision 3000 explicit opt-in preview',
    'source_commit': subprocess.check_output(['git', 'rev-parse', 'HEAD'], cwd=repo, text=True).strip(),
    'legacy_runtime_sha256': sha(base.read_bytes()), 'helper_sha256': helper_sha,
    'probe_sha256': probe_sha, 'probe_imports_match_legacy': True,
    'unchanged_driver_sha256': sha(files['cc2_ue300_r8152.ko'][0]),
    'unchanged_qualified_probe_sha256': sha(files['cc2_ue300_abi_probe.ko'][0]),
    'new_runtime_physical_validation': False, 'revision_3000_hardware_qualified': False,
    'automatic_catalog_changed': False,
}
files['validation.json'] = ((json.dumps(report, indent=2) + '\n').encode(), 0o644)
files['SHA256SUMS'] = (''.join(sha(data) + '  ' + name + '\n' for name, (data, mode) in sorted(files.items())
                              if name != 'SHA256SUMS').encode(), 0o644)
runtime = output / 'CC2-Connections-Runtime-RTL8153-3000.tar.gz'
with runtime.open('xb') as stream, gzip.GzipFile(filename='', mode='wb', fileobj=stream, mtime=0) as zipped, tarfile.open(fileobj=zipped, mode='w') as archive:
    for name, (data, mode) in sorted(files.items()):
        entry = tarfile.TarInfo(name)
        entry.size, entry.mode, entry.mtime = len(data), mode, 0
        archive.addfile(entry, io.BytesIO(data))
runtime_sha = sha(runtime.read_bytes())
lock_path = source / 'runtime.json'
lock = json.loads(lock_path.read_text())
lock.update(asset_id=0, release_id=0, filename=runtime.name, sha256=runtime_sha, helper_sha256=helper_sha)
lock['experimental_profiles'] = [{'usb_vendor': '0bda', 'usb_product': '8153', 'usb_revision': '3000',
                                  'explicit_consent_required': True, 'physical_qualification': False}]
lock['physical_reference'] = '31fd was tested in R5h; this rebuilt runtime and revision 3000 require separate hardware validation.'
lock_path.write_text(json.dumps(lock, indent=2) + '\n')
(app / 'src/connections/pin.h').write_text('#define CC2_CONNECTIONS_HELPER_SHA "' + helper_sha + '"\n')
embed = app / 'installer/network_component.py'
text = embed.read_text()
text, count = re.subn(r"^RUNTIME_SHA = '[a-f0-9]{64}'", "RUNTIME_SHA = '" + runtime_sha + "'", text, flags=re.M)
assert count == 1
text, count = re.subn(r"^HELPER_SHA = '[a-f0-9]{64}'", "HELPER_SHA = '" + helper_sha + "'", text, flags=re.M)
assert count == 1
embed.write_text(text)
for path in (runtime,):
    path.with_suffix(path.suffix + '.sha256').write_text(sha(path.read_bytes()) + '  ' + path.name + '\n')
(output / 'runtime-validation.json').write_text(json.dumps(report, indent=2) + '\n')
(output / 'runtime-lock.json').write_text(json.dumps(lock, indent=2) + '\n')
(output / 'generated-pins.patch').write_text(subprocess.check_output(['git', 'diff', '--',
    'experimental/network-connections/profile-pins.h', 'experimental/network-connections/runtime.json',
    'cc2-control/src/connections/profile-pins.h', 'cc2-control/src/connections/pin.h',
    'cc2-control/installer/network_component.py'], cwd=repo, text=True))
print('Preview runtime prepared; release IDs must be assigned before final source integration. No printer or catalog changed.')
