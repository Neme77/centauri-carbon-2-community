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
    for name in ('src/canvas_eject.h', 'tests/test_canvas_eject_api.c',
                 'web-src/preview/test-canvas-eject.mjs', 'tests/test_web_sync_static.py'):
        assert archive.read(name) == (BASE.parents[1] / 'cc2-control' / name).read_bytes(), name
    for name in ('index.html', 'locales/en.json', 'locales/fr.json', 'locales/it.json',
                 'locales/ru.json', 'locales/zh.json'):
        assert archive.read('firmware-integration/overlay/opt/inst/cc2-control/web/' + name) == archive.read('web/' + name), name
    for name in ('VERSION', 'FIRMWARE_VERSION', 'src/main.c', 'src/plates.h', 'tests/test_plates.c', 'tests/test_file_ops.c', 'tests/test_file_analysis.c', 'tests/test_file_analysis_http.py', 'docs/API.md', 'src/control.c', 'src/mqtt.c', 'src/uds.c', 'src/recovery.h', 'src/gcode_download.h', 'web/index.html', 'web-src/src/lib/meshdraw.ts', 'web-src/src/pages/bed.tsx', 'web-src/src/components/shared.tsx', 'web-src/src/lib/i18n.ts', 'web-src/preview/test-camera-stream.mjs', 'tests/test_runtime_limits.c', 'tests/test_recovery_download.c', 'tests/test_object_query.c', 'tests/test_control_actions.c', 'tests/test_installer_package.py', 'web-src/src/pages/dashboard.tsx', 'web-src/src/pages/control.tsx', 'web-src/preview/test-quick-actions.mjs', 'src/history.h', 'tests/test_printer_replies.c', 'tests/test_history_api.c', 'web-src/src/lib/machine.ts', 'web-src/src/lib/history.ts', 'web-src/src/pages/history.tsx', 'web-src/src/pages/print-dialog.tsx', 'tests/test_print_options.c', 'tests/test_camera_viewer_api.py', 'web-src/preview/test-print-history.mjs', 'web-src/preview/test-browser.mjs', 'web-src/preview/test-filament-mapping.mjs', 'web-src/preview/test-adaptive-mesh.mjs', 'tests/test_filament_metadata.c', 'tests/test_gcode_files.py', 'tests/test_c_harnesses.py', 'tests/test_canvas_discovery.c', 'src/mqtt.h', 'tests/test_pid_api.c', 'web-src/preview/test-pid.mjs', 'web-src/preview/test-print-temperatures.mjs', 'src/uds.h', 'tests/test_uds.c', 'web-src/src/components/layout.tsx', 'web-src/src/App.tsx', 'web-src/preview/test-printer-reports.mjs'):
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
    assert 'sleep 30' in launcher, name
    assert launcher.index(guard) < launcher.index('sleep 30') < launcher.index('exec '), name
