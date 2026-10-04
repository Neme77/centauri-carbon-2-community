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
    for name in ('src/main.c', 'src/control.c', 'src/mqtt.c', 'src/uds.c', 'src/recovery.h',
                 'src/gcode_download.h', 'web/index.html', 'web-src/src/lib/meshdraw.ts',
                 'web-src/src/pages/bed.tsx', 'web-src/src/components/shared.tsx', 'web-src/src/lib/i18n.ts',
                 'web-src/preview/test-camera-stream.mjs', 'tests/test_runtime_limits.c',
                 'tests/test_recovery_download.c', 'tests/test_object_query.c',
                 'tests/test_control_actions.c', 'tests/test_installer_package.py',
                 'web-src/src/pages/dashboard.tsx', 'web-src/src/pages/control.tsx',
                 'web-src/preview/test-quick-actions.mjs', 'tests/test_camera_viewer_api.py'):
        assert archive.read(name) == (BASE.parents[1] / 'cc2-control' / name).read_bytes(), name
    for lang in ('en', 'it', 'fr', 'zh', 'ru'):
        source = archive.read(f'web-src/public/locales/{lang}.json')
        assert source == archive.read(f'web/locales/{lang}.json')
    assert 'make' in (BASE / 'prepare.py').read_text()
    assert "'test', 'CROSS=', 'CC=gcc'" in (BASE / 'prepare.py').read_text()
print('PASS: pinned snapshot matches canonical backend, rebuilt UI, five locales and new regressions.')


# Builder runtime launchers are outside the standalone component snapshot.
for name in ('start.sh', 'launch.sh'):
    launcher = (BASE / 'components/cc2-control/runtime' / name).read_text()
    guard = "while ! pidof elegoo_printer >/dev/null 2>&1; do"
    assert guard in launcher, name
    assert 'sleep 60' in launcher, name
    assert launcher.index(guard) < launcher.index('sleep 60') < launcher.index('exec '), name
