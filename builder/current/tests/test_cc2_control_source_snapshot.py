"""The pinned standalone snapshot must contain the canonical validated component."""
from pathlib import Path
import hashlib
import importlib.util
import zipfile
BASE = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location('prepare_component', BASE / 'prepare.py')
prepare = importlib.util.module_from_spec(spec)
spec.loader.exec_module(prepare)
assert prepare.SOURCE_SHA256 == hashlib.sha256(prepare.SOURCE.read_bytes()).hexdigest()
with zipfile.ZipFile(prepare.SOURCE) as archive:
    assert archive.testzip() is None
    for name in ('src/main.c', 'src/mqtt.c', 'src/uds.c', 'src/recovery.h',
                 'src/gcode_download.h', 'web/index.html', 'web-src/src/lib/meshdraw.ts',
                 'web-src/src/pages/bed.tsx', 'web-src/src/components/shared.tsx',
                 'web-src/preview/test-camera-stream.mjs', 'tests/test_runtime_limits.c',
                 'tests/test_recovery_download.c'):
        assert archive.read(name) == (BASE.parents[1] / 'cc2-control' / name).read_bytes(), name
    for lang in ('en', 'it', 'fr', 'zh'):
        source = archive.read(f'web-src/public/locales/{lang}.json')
        assert source == archive.read(f'web/locales/{lang}.json')
    assert 'make' in (BASE / 'prepare.py').read_text()
    assert "'test', 'CROSS=', 'CC=gcc'" in (BASE / 'prepare.py').read_text()
print('PASS: pinned snapshot matches canonical backend, rebuilt UI, four locales and new regressions.')

