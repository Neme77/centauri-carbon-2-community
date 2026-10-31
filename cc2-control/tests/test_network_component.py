"""The signed installer can omit unsupported hardware without changing networking."""
from pathlib import Path
import hashlib
import importlib.util
import json
import os
import subprocess
import tempfile

app = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location('network_component', app / 'installer/network_component.py')
module = importlib.util.module_from_spec(spec)
spec.loader.exec_module(module)
runtime = os.environ.get('CC2_TEST_CONNECTIONS_RUNTIME')
if not runtime:
    # Ordinary app-only host suites do not need to download runtime binaries.
    # The candidate job supplies and checks the exact physically accepted input.
    print('PASS: Connections embedding is optional in app-only fixtures')
else:
    text = module.functions(Path(runtime))
    assert text.count('sh "$NETWORK_SOURCE/install.sh"') == 1
    assert module.RUNTIME_SHA in text
    assert 'insmod' not in text and 'reboot' not in text
    with tempfile.TemporaryDirectory(prefix='cc2-network-embed-') as temporary:
        base = Path(temporary)
        for shell in (['dash'], ['busybox', 'sh']):
            for scenario in ('unsupported', 'unsupported-existing', 'supported'):
                fixture = base / (shell[0] + '-' + scenario)
                fixture.mkdir()
                stage = fixture / 'stage'
                stage.mkdir(mode=0o700)
                manager = fixture / 'existing-manager'
                if scenario == 'unsupported-existing':
                    manager.mkdir()
                    (manager / 'keep').write_text('untouched')
                stop = text.index('\n}\n') + 3
                profile = 'network_profile_supported() { return ' + ('0' if scenario == 'supported' else '1') + '; }\n'
                code = profile + text[stop:]
                code = code.replace('/opt/usr/cc2-network-manager', str(manager))
                # Extract the actual archive, but never execute its native installer.
                code = code.replace('sh "$NETWORK_SOURCE/install.sh"', "printf 'FIXTURE_COMPONENT_INSTALL\\n'")
                script = fixture / 'run.sh'
                script.write_text('set -eu\nSTAGE="' + str(stage) + '"\n' + code + '\nnetwork_install_component\n')
                subprocess.run([*shell, '-n', script], check=True, timeout=10)
                result = subprocess.run([*shell, script], capture_output=True, text=True, timeout=30)
                assert (result.returncode == 0) == (scenario != 'unsupported-existing'), result.stderr
                assert not list(stage.iterdir()), scenario
                if scenario == 'supported':
                    assert 'FIXTURE_COMPONENT_INSTALL' in result.stdout
                    assert 'CONNECTIONS_COMPONENT_INSTALLED=1' in result.stdout
                else:
                    assert 'FIXTURE_COMPONENT_INSTALL' not in result.stdout
                if manager.exists():
                    assert (manager / 'keep').read_text() == 'untouched'
    print('PASS: exact embedded runtime, GNU/BusyBox syntax, guarded unsupported-profile skip, existing-manager refusal and successful unpack/cleanup; no native installer executed')
