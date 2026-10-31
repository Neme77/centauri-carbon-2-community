"""Host/ARM fixture qualification of the committed persistent Connections manager."""
from pathlib import Path
import json
import os
import subprocess
import tempfile

root = Path(__file__).resolve().parent
repo = root.parents[1]
def run(args, **kw):
    print('+', *map(str, args), flush=True)
    return subprocess.run(list(map(str, args)), check=True, timeout=300, **kw)

assert os.geteuid() == 0, 'Run the fixture suite as root; all devices/services are stand-ins'
for name in ('test_profile_cache.py', 'test_seed.py', 'test_routes.py', 'test_wifi_enable.py',
             'test_hooks.py', 'test_install_diagnostics.py', 'test_wifi_entry.py',
             'test_runner.py', 'test_unix.py'):
    run(['python3', root / name])
run(['python3', root / 'test_reactor_startup.py', root / 'fixtures/legacy-prepare-hooks.awk'])
flags = ['-std=c11', '-Os', '-Wall', '-Wextra', '-Wpedantic', '-Werror']
with tempfile.TemporaryDirectory(prefix='cc2-public-fixture-') as tmp:
    base = Path(tmp)
    run(['gcc', *flags, '-fsanitize=address,undefined', '-fno-sanitize-recover=all', root / 'test_core.c', '-lm', '-o', base / 'core'])
    run([base / 'core'])
    component = base / 'component'
    component.mkdir(mode=0o700)
    defines = [f'-DNG_{key}="{value}"' for key, value in {
        'COMPONENT': component, 'STATE': component / 'state', 'RUN': component / 'run',
        'TEST_TOOLS': component / 'tools', 'WPA': '/tmp/cc2-connections-wpa-test.sock'}.items()]
    run(['gcc', *flags, '-DNG_TEST', *defines, root / 'test_manager.c', '-lm', '-o', component / 'control'])
    run([component / 'control'])
    run(['python3', root / 'test_wifi.py', component / 'control'])
    run(['arm-linux-gnueabihf-gcc', *flags, '-static', '-s', root / 'manager.c', '-lm', '-o', base / 'control-arm'])
    result = run(['qemu-arm', base / 'control-arm', '--status'], capture_output=True, text=True)
    assert json.loads(result.stdout)['automatic_selection'] is False
run(['python3', repo / 'cc2-control/tests/test_connections_sources.py'])
print('PUBLIC CONNECTIONS FIXTURES PASSED. No real printer, module or native service was accessed.', flush=True)
