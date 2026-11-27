"""The pinned standalone snapshot must contain the canonical validated component."""
from pathlib import Path
import hashlib
import json
import importlib.util
import zipfile
BASE = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location('prepare_component', BASE / 'prepare.py')
prepare = importlib.util.module_from_spec(spec)
spec.loader.exec_module(prepare)
assert prepare.SOURCE_SHA256 == hashlib.sha256(prepare.SOURCE.read_bytes()).hexdigest()
with zipfile.ZipFile(prepare.SOURCE) as archive:
    assert archive.testzip() is None
    for name in ('src/canvas_unload.h', 'tests/test_canvas_unload.c', 'web-src/src/pages/canvas-unload.tsx', 'web-src/preview/test-canvas-unload.mjs', 'VERSION', 'FIRMWARE_VERSION', 'src/main.c', 'src/plates.h', 'tests/test_plates.c', 'tests/test_file_ops.c', 'tests/test_file_analysis.c', 'tests/test_file_analysis_http.py', 'docs/API.md', 'src/control.c', 'src/mqtt.c', 'src/uds.c', 'src/recovery.h', 'src/gcode_download.h', 'web/index.html', 'web-src/src/lib/meshdraw.ts', 'web-src/src/pages/bed.tsx', 'web-src/src/components/shared.tsx', 'web-src/src/lib/i18n.ts', 'web-src/preview/test-camera-stream.mjs', 'tests/test_runtime_limits.c', 'tests/test_recovery_download.c', 'tests/test_object_query.c', 'tests/test_control_actions.c', 'tests/test_installer_package.py', 'web-src/src/pages/dashboard.tsx', 'web-src/src/pages/control.tsx', 'web-src/preview/test-quick-actions.mjs', 'src/history.h', 'tests/test_printer_replies.c', 'tests/test_history_api.c', 'web-src/src/lib/machine.ts', 'web-src/src/lib/history.ts', 'web-src/src/pages/history.tsx', 'web-src/src/pages/print-dialog.tsx', 'tests/test_print_options.c', 'tests/test_camera_viewer_api.py', 'web-src/preview/test-print-history.mjs', 'web-src/preview/test-browser.mjs', 'web-src/preview/test-filament-mapping.mjs', 'web-src/preview/test-adaptive-mesh.mjs', 'tests/test_filament_metadata.c', 'tests/test_gcode_files.py', 'tests/test_c_harnesses.py', 'tests/test_canvas_discovery.c', 'src/mqtt.h', 'tests/test_pid_api.c', 'web-src/preview/test-pid.mjs', 'web-src/preview/test-print-temperatures.mjs', 'src/uds.h', 'tests/test_uds.c', 'web-src/src/components/layout.tsx', 'web-src/src/App.tsx', 'web-src/preview/test-printer-reports.mjs', 'src/appearance.h', 'src/display_theme.h', 'src/connections/json_body.h', 'src/connections/json_large.h', 'tests/test_appearance_capacity.py', 'tests/test_display_capacity.py', 'tests/test_theme_contract_static.py', 'tests/test_display_limits_static.py', 'web-src/src/lib/appearance.ts', 'web-src/src/lib/display.ts', 'web-src/src/pages/appearance.tsx', 'web-src/preview/test-display-pack.mjs', 'web-src/vite.config.ts'):
        assert archive.read(name) == (BASE.parents[1] / 'cc2-control' / name).read_bytes(), name
    locales = sorted((BASE.parents[1] / 'cc2-control/web-src/public/locales').glob('*.json'))
    assert 'hu.json' in [p.name for p in locales]
    assert sorted(p.split('/')[-1] for p in archive.namelist() if p.startswith('web-src/public/locales/') and p.endswith('.json')) == [p.name for p in locales]
    assert sorted(p.split('/')[-1] for p in archive.namelist() if p.startswith('web/locales/') and p.endswith('.json')) == [p.name for p in locales]
    for locale in locales:
        source = archive.read(f'web-src/public/locales/{locale.name}')
        assert source == locale.read_bytes(), locale.name
        # The shipped catalogue is compact JSON (web-src/vite.config.ts
        # compactLocales) to save memory and transfer size on the printer; the
        # source stays indented for review. Compare parsed content, as
        # cc2-control/tests/test_web_sync_static.py does in the app tree.
        shipped = archive.read(f'web/locales/{locale.name}')
        assert json.loads(shipped) == json.loads(source), locale.name
        assert b'\n' not in shipped.rstrip(b'\n'), locale.name
    # Guides are shipped in source.zip too, including translated companions.
    for document in sorted((BASE.parents[1] / 'cc2-control/docs').rglob('*.md')):
        member = document.relative_to(BASE.parents[1] / 'cc2-control').as_posix()
        assert archive.read(member) == document.read_bytes(), member
    assert 'make' in (BASE / 'prepare.py').read_text()
    assert "'test', 'CROSS=', 'CC=gcc'" in (BASE / 'prepare.py').read_text()
print('PASS: pinned snapshot matches canonical backend, rebuilt UI, all shipped locales including Hungarian and new regressions.')


# Builder runtime launchers are outside the standalone component snapshot.
for name in ('start.sh', 'launch.sh'):
    launcher = (BASE / 'components/cc2-control/runtime' / name).read_text()
    guard = "while ! pidof elegoo_printer >/dev/null 2>&1; do"
    assert guard in launcher, name
    assert 'sleep 30' in launcher, name
    assert launcher.index(guard) < launcher.index('sleep 30') < launcher.index('exec '), name
