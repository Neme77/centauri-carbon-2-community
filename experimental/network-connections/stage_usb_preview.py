"""Retain a verified runtime in a DRAFT release and commit its exact source pins.

No stable release, catalog or existing release asset is modified. Called only
after the candidate jobs pass, on the explicit preview branch and commit.
"""
from pathlib import Path
import hashlib
import json
import re
import subprocess
import sys
import tarfile

source = Path(__file__).resolve().parent
repo = source.parents[1]
runtime_dir = Path(sys.argv[1]).resolve()
expected = sys.argv[2]
repository = sys.argv[3]
assert repository == 'Neme77/centauri-carbon-2-community'
assert re.fullmatch('[0-9a-f]{40}', expected)
branch = 'fix/rtl8153-opt-in-274'
def run(args, **kwargs):
    return subprocess.run(list(map(str, args)), check=True, timeout=180, **kwargs)
def read(args):
    return subprocess.check_output(list(map(str, args)), text=True).strip()
assert read(['git', 'rev-parse', 'HEAD']) == expected
assert not read(['git', 'status', '--porcelain'])
assert read(['git', 'ls-remote', 'origin', 'refs/heads/' + branch]).split()[0] == expected
lock = json.loads((runtime_dir / 'runtime-lock.json').read_text())
runtime = runtime_dir / lock['filename']
assert hashlib.sha256(runtime.read_bytes()).hexdigest() == lock['sha256']
with tarfile.open(runtime) as archive:
    helper = archive.extractfile('control').read()
    probe = archive.extractfile('cc2_ue300_abi_probe_3000.ko').read()
    validated_source = json.loads(archive.extractfile('validation.json').read())['source_commit']
assert hashlib.sha256(helper).hexdigest() == lock['helper_sha256']
probe_sha = hashlib.sha256(probe).hexdigest()
assert re.fullmatch('[0-9a-f]{40}', validated_source)
# Reuse is allowed only for a publication-runner fix, never production changes.
changed = set(read(['git', 'diff', '--name-only', validated_source, 'HEAD']).splitlines())
assert changed <= {'.github/workflows/rtl8153-preview.yml', 'experimental/network-connections/stage_usb_preview.py'}
tag = 'rtl8153-3000-preview-' + validated_source[:12]
releases = [release for page in json.loads(read(['gh', 'api', 'repos/' + repository + '/releases', '--paginate', '--slurp'])) for release in page]
matches = [release for release in releases if release['tag_name'] == tag]
if not matches:
    notes = runtime_dir / 'draft-notes.md'
    notes.write_text('''Private RTL8153 revision 3000 opt-in candidate for issue #274.

This is a draft component runtime, not a stable application or firmware release.
No automatic-update catalog entry is changed. Host/ARM, UI and packaging checks
are separate from device validation; revision 3000 remains experimental.
The immutable R5h driver, MII module and 31fd probe are unchanged. Corresponding
GPL sources, the new probe source and exact checksums are included in the archive.
''')
    run(['gh', 'release', 'create', tag, '--repo', repository, '--draft', '--target', validated_source,
         '--title', 'RTL8153 revision 3000 opt-in candidate', '--notes-file', notes,
         runtime, runtime.with_suffix(runtime.suffix + '.sha256')])
    releases = [release for page in json.loads(read(['gh', 'api', 'repos/' + repository + '/releases', '--paginate', '--slurp'])) for release in page]
    matches = [release for release in releases if release['tag_name'] == tag]
assert len(matches) == 1
release = matches[0]
assert release['draft'] and release['target_commitish'] == validated_source
assets = [asset for asset in release['assets'] if asset['name'] == runtime.name]
assert len(assets) == 1 and assets[0]['digest'] == 'sha256:' + lock['sha256']
lock.update(asset_id=assets[0]['id'], release_id=release['id'])
(source / 'runtime.json').write_text(json.dumps(lock, indent=2) + '\n')
for pins in (source / 'profile-pins.h', repo / 'cc2-control/src/connections/profile-pins.h'):
    text, count = re.subn(r'(#define NG_CC2_UE300_ABI_PROBE_3000_KO_SHA\s*\\\s*)"[a-f0-9]{64}"',
                         lambda m: m[1] + '"' + probe_sha + '"', pins.read_text())
    assert count == 1
    pins.write_text(text.replace('/* Replaced by the reproducible probe build before packaging/publication. */\n', ''))
(repo / 'cc2-control/src/connections/pin.h').write_text('#define CC2_CONNECTIONS_HELPER_SHA "' + lock['helper_sha256'] + '"\n')
embed = repo / 'cc2-control/installer/network_component.py'
text = embed.read_text()
for macro, value in (('RUNTIME_SHA', lock['sha256']), ('HELPER_SHA', lock['helper_sha256'])):
    text, count = re.subn(r"^" + macro + r" = '[a-f0-9]{64}'", macro + " = '" + value + "'", text, flags=re.M)
    assert count == 1
embed.write_text(text)
run(['git', 'config', 'user.name', 'github-actions[bot]'])
run(['git', 'config', 'user.email', '41898282+github-actions[bot]@users.noreply.github.com'])
paths = ['experimental/network-connections/runtime.json', 'experimental/network-connections/profile-pins.h',
         'cc2-control/src/connections/profile-pins.h', 'cc2-control/src/connections/pin.h',
         'cc2-control/installer/network_component.py']
run(['git', 'add', '--', *paths])
run(['git', 'commit', '-m', 'Pin verified RTL8153 preview runtime and probe'])
assert not read(['git', 'status', '--porcelain'])
run(['python3', 'experimental/pr128-integration/sync_source.py'])
run(['git', 'add', '--', 'builder/current/components/cc2-control/source/source.zip',
     'builder/current/prepare.py', 'builder/current/core/firmware_builder.py',
     'builder/current/launch_helpers/run_build.py'])
run(['git', 'commit', '-m', 'Refresh builder source snapshot for RTL8153 preview'])
run(['python3', 'builder/current/tests/test_cc2_control_source_snapshot.py'])
assert not read(['git', 'status', '--porcelain'])
run(['git', 'push', 'origin', 'HEAD:refs/heads/' + branch])
result = {'release_id': release['id'], 'asset_id': assets[0]['id'], 'draft': True,
          'source_head': read(['git', 'rev-parse', 'HEAD']), 'runtime': lock}
(runtime_dir / 'staged-draft.json').write_text(json.dumps(result, indent=2) + '\n')
print('Verified component retained in draft; exact pins and final builder snapshot committed. Stable catalog unchanged.')
