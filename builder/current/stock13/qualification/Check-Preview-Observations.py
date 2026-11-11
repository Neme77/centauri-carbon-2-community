"""Release-local checks for issue #327; never contact the printer."""
from contextlib import ExitStack
import hashlib
import importlib.util
import json
import os
from pathlib import Path
import re
import subprocess
import sys
from tempfile import TemporaryDirectory
import unittest

HERE = Path(__file__).resolve().parent
PORT = HERE.parents[1] / 'work/cc2-13-port'
SOURCE = PORT / 'source'
CORE = SOURCE / 'builder/current/core'
sys.path.insert(0, str(CORE))
import startup_stock13 as startup
import firmware_builder as firmware


def load(name, path):
    spec = importlib.util.spec_from_file_location(name, path)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def stage_launchers(root):
    reactor = PORT / 'reactor13-prepared'
    component = PORT / 'native13-check/combined-rfid-exhaust'
    receipt = json.loads((component / 'native13-check.json').read_text())
    hook = root / 'etc/init.d/cc2-reactor-launch'
    hook.parent.mkdir(parents=True)
    hook.write_bytes((reactor / 'firmware-launcher.sh').read_bytes())
    (root / 'opt/inst').mkdir(parents=True)
    startup.stage_printer_extensions(root, component, reactor, receipt['printer_sha256'])
    generated = hook.read_text()
    assert 'STOP=legacy_reactor_disable_marker' in generated
    assert '[ ! -e /opt/usr/cc2-reactor-disabled ] || fallback' not in generated
    assert 'exit 125' in generated
    assert 'cc2_boot_note extensions' in (root / 'opt/inst/cc2-printer-extensions13/launch.sh').read_text()
    return generated


def marker_checks(shell):
    for case in ('empty', 'nonempty', 'symlink', 'status-symlink'):
        with TemporaryDirectory(prefix='cc2-marker-') as scratch:
            root = Path(scratch)
            script = stage_launchers(root)
            usr = root / 'opt/usr'
            usr.mkdir(parents=True)
            marker = usr / 'cc2-reactor-disabled'
            if case == 'symlink':
                marker.symlink_to(root / 'absent')
            else:
                marker.write_bytes(b'unknown' if case == 'nonempty' else b'')
            sentinel = root / 'sentinel'
            sentinel.write_bytes(b'preserve')
            if case == 'status-symlink':
                (usr / 'cc2-reactor-boot.status').symlink_to(sentinel)
            proc = root / 'proc'
            proc.mkdir()
            (proc / 'mounts').write_text(f'fake {usr} ext4 rw 0 0\n')
            script = re.sub(r'/opt/usr|/opt/bin|/opt/inst|/proc|/tmp',
                            lambda m: str(root / m[0].lstrip('/')), script)
            # Only replace UID at the fixture boundary; use actual shell/file IO.
            script = script.replace('set -eu\n', 'set -eu\nid(){ echo 0; }\n', 1)
            result = subprocess.run(shell + ['-s'], input=script.encode(), capture_output=True, timeout=10)
            assert result.returncode == 125, (case, result.stderr)
            assert b'legacy Reactor disable marker' in result.stderr
            assert marker.exists() or marker.is_symlink()
            if case != 'status-symlink':
                assert 'STOP=legacy_reactor_disable_marker' in (usr / 'cc2-reactor-boot.status').read_text()
            assert sentinel.read_bytes() == b'preserve'
            print(f'PASS {shell[-1]} native boot marker {case}', flush=True)


preparation = load('preview_prepare_tests', HERE / 'Test-Prepare-Upgrade.py')
connections = load('preview_connections_tests', SOURCE / 'builder/current/tests/test_connections_firmware.py')
for relative in ('builder/current/components/connections/firmware-seed.sh',
                 'builder/current/components/connections/firmware.init'):
    firmware.reqhash(SOURCE / relative, firmware.INTEGRATION_INPUTS[relative], relative)
suite = unittest.defaultTestLoader.loadTestsFromTestCase(connections.ConnectionsInputTests)
assert unittest.TextTestRunner().run(suite).wasSuccessful()
with TemporaryDirectory(prefix='cc2-generated-') as scratch:
    stage_launchers(Path(scratch))
print('PASS exact .13 launcher generation and retained source pins', flush=True)

if '--host-only' in sys.argv:
    print('HOST CHECKS ONLY: shell fixtures and ARM BusyBox checks remain pending')
    raise SystemExit(0)
assert os.name != 'nt', 'Use Check-Preview-Observations.cmd to run the Linux fixtures'
with ExitStack() as stack:
    shells = [['sh'], preparation.stock_busybox(stack) + ['ash']]
    for shell in shells:
        preparation.check(shell)
        marker_checks(shell)
        test = connections.ConnectionsBootstrapTests()
        for case in ('not-root', 'fresh', 'retained', 'idempotent', 'foreign-dhcp', 'retained-damage',
                     'damaged-source', 'mapped', 'running', 'native-running', 'copy-failure',
                     'component-move-failure', 'dhcp-move-failure', 'seed-failure',
                     'retained-native-damage', 'retained-link', 'retained-manifest-escape',
                     'retained-damage-seed-failure', 'idempotent-damage'):
            test.fixture(shell, case)
            print(f'PASS {shell[-1]} Connections migration {case}', flush=True)
print('PASS OFFLINE PREVIEW OBSERVATIONS: host and stock ARM BusyBox; no printer accessed')
