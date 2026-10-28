"""Test the actual shell status fragment with a wget that rejects -T."""
import json
import os
from pathlib import Path
import subprocess
import tempfile

source = Path(__file__).with_name('probe.sh').read_text()
start = source.index('printer=$(')
stop = source.index("echo 'CHECK PASS:", start)
fragment = source[start:stop]
with tempfile.TemporaryDirectory() as directory:
    root = Path(directory)
    wget = root / 'wget'
    wget.write_text('''#!/bin/sh
for argument in "$@"; do
    case "$argument" in -T|-T*) exit 139;; esac
done
[ "$*" = '-qO- http://127.0.0.1:8081/api/printer' ] || exit 64
[ "${CC2_TEST_WGET_FAIL:-0}" = 0 ] || exit 8
printf '%s' "$CC2_TEST_RESPONSE"
''')
    wget.chmod(0o755)
    env = dict(os.environ, PATH=str(root) + os.pathsep + os.environ['PATH'])
    old = subprocess.run(['sh', '-c', 'wget -T 5 -qO- http://127.0.0.1:8081/api/printer'], env=env, capture_output=True)
    assert old.returncode == 139
    printer = {'connected': True, 'last_message_age': 0, 'extruder': {'temperature': 25.0, 'target': 0.0}, 'heater_bed': {'temperature': 25.0, 'target': 0.0}, 'machine': {'status': 1, 'status_name': 'Idle'}}
    def run(label, value, expected, fail=False):
        response = json.dumps(value, separators=(',', ':')) if isinstance(value, dict) else value
        result = subprocess.run(['sh', '-c', 'set -eu\nfail() { echo "STOP: $*" >&2; exit 1; }\n' + fragment + '\necho STATUS_GUARDS_PASS'], env=dict(env, CC2_TEST_RESPONSE=response, CC2_TEST_WGET_FAIL='1' if fail else '0'), capture_output=True, text=True, timeout=5)
        assert (result.returncode == 0) == expected, (label, result.stdout, result.stderr)
        print('PASS:', label)
    run('compatible wget accepts fresh Idle and zero targets', printer, True)
    run('request failure stops', printer, False, True)
    run('malformed reply stops', '<html>error</html>', False)
    for label, key, value in [('disconnected', 'connected', False), ('stale', 'last_message_age', 16), ('printing', 'machine', {'status': 2})]:
        run(label + ' refuses', dict(printer, **{key: value}), False)
    run('nozzle heating refuses', dict(printer, extruder={'target': 200.0}), False)
    run('bed heating refuses', dict(printer, heater_bed={'target': 60.0}), False)
print('PASS: rejected -T and retained guards; no printer contacted')
