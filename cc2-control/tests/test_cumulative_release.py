"""Public release packaging cannot silently omit the camera migration."""
from pathlib import Path
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]
for arguments in ([], ['--release']):
    result = subprocess.run([sys.executable, str(ROOT / 'installer/package.py'), *arguments], capture_output=True, text=True)
    assert result.returncode == 2
    assert 'Public cumulative releases require --camera-runtime' in result.stderr
result = subprocess.run([sys.executable, str(ROOT / 'installer/package.py'), '--release', '--application-only-trial'], capture_output=True, text=True)
assert result.returncode == 2 and 'mutually exclusive' in result.stderr
print('PASS public release packaging refuses to omit the camera component for users skipping 1.2.19')
