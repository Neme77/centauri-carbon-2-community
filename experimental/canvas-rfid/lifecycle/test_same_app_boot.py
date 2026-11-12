"""Actual same-app boot worker on private fixture files; no printer or UART."""
from pathlib import Path
import hashlib
import json
import os
import shutil
import subprocess
import tempfile

from test_stage_worker import fixture, put

HERE = Path(__file__).resolve().parent
sha = lambda data: hashlib.sha256(data).hexdigest()
NAMES = ('coordinator', 'capture', 'app-integration.json')
OLD = {name: ('old ' + name).encode() for name in NAMES}
NEW = {name: ('new ' + name).encode() for name in NAMES}
CASES = ('active', 'repeat', 'rollback', 'absent', 'dormant', 'source-tampered',
         'target-tampered', 'backup-bad', 'transfer-bad', 'pending',
         'activation-request', 'busy', 'changed-generation', 'guard-bad', 'foreign-app')


def run(architectures):
    assert os.geteuid() == 0
    stock = Path(os.environ['CC2_TEST_STOCK_CANVAS']).read_bytes()
    assert sha(stock) == 'f1267ee3219dcad73efffa8dc38e30ff0f17c02c0a1674cbead48a99bdff3228'
    app_bytes = b'#!/bin/sh\n[ "$1" = --installer-native-idle ]\n'
    transfer = b'unchanged transfer worker fixture'
    count = 0
    flags = ['-O2', '-std=c11', '-Wall', '-Wextra', '-Wpedantic', '-Werror', '-D_POSIX_C_SOURCE=200809L']
    with tempfile.TemporaryDirectory(prefix='cc2-same-app-boot-') as temporary:
        root = Path(temporary)
        for arch, compiler, extra, runner in architectures:
            folder = root / arch

            def setup():
                if folder.exists():
                    shutil.rmtree(folder)
                component, tree, native, app, _, _, profile, _ = fixture(folder, app_bytes, stock)
                incoming = folder / 'incoming'
                incoming.mkdir(mode=0o700)
                for name in NAMES:
                    mode = 0o600 if name.endswith('.json') else 0o700
                    put(component / name, OLD[name], mode)
                    put(incoming / name, NEW[name], mode)
                put(component / 'transfer-worker', transfer, 0o700)
                put(component / 'original-stock-44.bin', stock)
                from test_stage_worker import seed_journal
                seed_journal(component, profile, 5)
                return component, incoming, app, profile

            component, incoming, app, profile = setup()
            plan = root / (arch + '-plan.h')
            rows = ','.join('{%s,%s,%s,0%o}' % (json.dumps(name), json.dumps(sha(OLD[name])),
                            json.dumps(sha(NEW[name])), 0o600 if name.endswith('.json') else 0o700) for name in NAMES)
            plan.write_text('static const struct au_plan au_boot_plan={%s,%s,%s,{%s}};\n' %
                            (json.dumps(sha(app_bytes)), json.dumps(sha(app_bytes)), json.dumps(sha(profile)), rows))
            defines = {'AU_BOOT_PLAN': str(plan), 'AU_COMPONENT': str(component),
                       'AU_DIRECTORY': str(folder / 'transaction'), 'AU_APP': str(app),
                       'AU_BARRIER': str(folder / 'barrier'), 'AU_INCOMING': str(incoming),
                       'AU_TRANSFER_SHA': sha(transfer)}
            binary = root / (arch + '-worker')
            subprocess.run([compiler, *flags, *extra, '-DAU_SAME_APP_HELPER_UPDATE=1',
                            *['-D' + key + '=' + json.dumps(value) for key, value in defines.items()],
                            str(HERE / 'immutable_app_binding.c'), '-o', str(binary)], check=True, timeout=120)
            for case in CASES:
                component, incoming, app, profile = setup()
                journal = (component / 'lifecycle').read_bytes()
                guard = (component / 'libcc2-rfid.so').read_bytes()
                if case == 'absent':
                    shutil.rmtree(component)
                elif case == 'dormant':
                    (component / 'lifecycle').unlink()
                    (component / 'enabled').unlink()
                elif case == 'source-tampered':
                    put(incoming / 'capture', b'foreign', 0o700)
                elif case == 'target-tampered':
                    put(component / 'capture', b'foreign', 0o700)
                elif case == 'backup-bad':
                    put(component / 'original-stock-44.bin', b'foreign')
                elif case == 'transfer-bad':
                    put(component / 'transfer-worker', b'foreign', 0o700)
                elif case == 'pending':
                    put(component / 'lifecycle.next', b'pending')
                elif case == 'activation-request':
                    put(component / 'activation-request', b'pending')
                elif case == 'busy':
                    (folder / 'barrier').mkdir(mode=0o700)
                elif case == 'guard-bad':
                    put(component / 'libcc2-rfid.so', b'foreign')
                elif case == 'foreign-app':
                    put(app, b'foreign', 0o755)
                before = {name: (component / name).read_bytes() for name in NAMES} if case != 'absent' else None
                result = subprocess.run([*runner, str(binary)], capture_output=True, text=True, timeout=20)
                expected = case in ('active', 'repeat', 'rollback', 'absent', 'dormant', 'changed-generation')
                assert (result.returncode == 0) == expected, (arch, case, result.stderr)
                if case == 'absent':
                    assert not component.exists()
                else:
                    assert {name: (component / name).read_bytes() for name in NAMES} == (NEW if expected and case != 'dormant' else before)
                    if case != 'dormant':
                        assert (component / 'lifecycle').read_bytes() == journal
                    if case not in ('backup-bad',):
                        assert (component / 'original-stock-44.bin').read_bytes() == stock
                    if case != 'guard-bad':
                        assert (component / 'libcc2-rfid.so').read_bytes() == guard
                    if case != 'transfer-bad':
                        assert (component / 'transfer-worker').read_bytes() == transfer
                if case == 'repeat':
                    assert subprocess.run([*runner, str(binary)], capture_output=True, timeout=20).returncode == 0
                if case == 'rollback':
                    rolled = subprocess.run([*runner, str(binary), '--rollback'], capture_output=True, timeout=20)
                    assert rolled.returncode == 0, (arch, case, rolled.stderr)
                    assert {name: (component / name).read_bytes() for name in NAMES} == OLD
                    assert (component / 'lifecycle').read_bytes() == journal
                if case == 'changed-generation':
                    import struct
                    changed = bytearray(journal[:128])
                    struct.pack_into('<Q', changed, 24, 2)
                    put(component / 'lifecycle', changed + sha(changed).encode())
                    refused = subprocess.run([*runner, str(binary)], capture_output=True, timeout=20)
                    assert refused.returncode != 0
                    assert (component / 'lifecycle').read_bytes() == changed + sha(changed).encode()
                assert (folder / 'barrier').exists() == (case == 'busy')
                count += 1
                print(f'PASS {arch} same-app boot migration {case}', flush=True)
    print(f'PASS: {count} actual same-app boot migrations; lifecycle, guard and original backup preserved')
    return count
