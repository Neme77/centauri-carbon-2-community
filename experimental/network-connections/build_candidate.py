"""Build the final-version cumulative candidate; no printer or release is changed."""
from pathlib import Path
import hashlib
import json
import os
import subprocess
import sys
import urllib.request

root = Path(__file__).resolve().parent
repo = root.parents[1]
app = repo / 'cc2-control'
output, runtime = Path(sys.argv[1]).resolve(), Path(sys.argv[2]).resolve()
output.mkdir(parents=True, exist_ok=False)
def run(args, **kw):
    print('+', *map(str, args), flush=True)
    return subprocess.run(list(map(str, args)), check=True, timeout=600, **kw)

baseline = output / 'camera-baseline.zip'
url = ('https://github.com/Neme77/centauri-carbon-2-community/releases/download/'
       'CC2-Control-1.2.23-V4.4/CC2-Control-1.2.23-Multiplatform-Update.zip')
with urllib.request.urlopen(url, timeout=60) as source, baseline.open('xb') as target:
    length = 0
    while chunk := source.read(65536):
        length += len(chunk)
        if length > 32 * 1024 * 1024:
            raise ValueError('Unexpected camera baseline size')
        target.write(chunk)
camera = output / 'camera-runtime'
run([sys.executable, root / 'extract_camera.py', baseline, camera])
run([sys.executable, app / 'tests/test_network_component.py'], env=dict(os.environ, CC2_TEST_CONNECTIONS_RUNTIME=str(runtime)))
run(['make', '-C', app, 'clean'])
run(['make', '-C', app, 'all', 'EXTRA_CFLAGS=-Werror'])
binary = app / 'dist/cc2-control/cc2-control'
run([sys.executable, app / 'tests/test_smoke.py', binary], env=dict(os.environ, CC2_TEST_RUNNER='qemu-arm'))
version = (app / 'VERSION').read_text().strip()
ordinary = output / f'CC2-Control-{version}-Multiplatform-Update.zip'
run([sys.executable, app / 'installer/package.py', '--release', '--camera-runtime', camera, '--connections-runtime', runtime,
     '--prepared', app / 'dist/cc2-control', '--output', ordinary])
cumulative = output / f'CC2-Control-{version}-Connections-Update.zip'
run([sys.executable, root / 'package_cumulative.py', '--app-updater', ordinary,
     '--runtime', runtime, '--output', cumulative])
run([sys.executable, root / 'test_public_package.py', ordinary, runtime])
result = {
    'version': version, 'source_commit': subprocess.check_output(['git', 'rev-parse', 'HEAD'], cwd=repo, text=True).strip(),
    'binary_sha256': hashlib.sha256(binary.read_bytes()).hexdigest(),
    'runtime_sha256': hashlib.sha256(runtime.read_bytes()).hexdigest(),
    'arm_smoke_passed': True, 'public_package_regressions_passed': True,
    'physical_runtime_reference': 'R5h accepted separately by the maintainer',
    'new_cumulative_physical_validation': False,
    'stable_release_published': False,
    'sha256': {p.name: hashlib.sha256(p.read_bytes()).hexdigest() for p in output.glob('*.zip') if p != baseline},
}
(output / 'candidate-validation.json').write_bytes((json.dumps(result, indent=2) + '\n').encode())
# Keep recovered camera binaries only as build inputs, not extra user downloads.
print('CUMULATIVE CANDIDATE READY FOR MAINTAINER DEVICE TEST. No printer or release changed.', flush=True)
