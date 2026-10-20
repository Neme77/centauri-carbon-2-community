"""Linux shell/transaction fixtures. No printer binary or real service is used."""
import hashlib
import os
import re
from pathlib import Path
import shutil
import stat
import subprocess
import tempfile

HERE = Path(__file__).resolve().parent
NAMES = ('common.sh', 'manage.sh', 'boot-camera.sh')
ORIGINAL = b'''#!/bin/sh
start() {
    echo unchanged-gui
    ai_camera &
    echo unchanged-mosquitto
}
case "${1:-}" in start) start;; esac
'''
IDLE = '{"connected":true,"last_message_age":2,"machine":{"status":1,"status_name":"Idle"}}'
HASH = lambda data: hashlib.sha256(data).hexdigest()
CASES = 0

def write(path, text, executable=False):
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(text)
    path.chmod(0o755 if executable else 0o600)

def manifest(folder):
    names = (*NAMES, 'libcc2-camera-reap-r2.so', 'cc2-camera-reap-check')
    (folder / 'SHA256SUMS').write_text(''.join(HASH((folder / n).read_bytes()) + '  ' + n + '\n' for n in sorted(names)))

class Fixture:
    def __init__(self, parent, noop):
        self.root = Path(tempfile.mkdtemp(prefix='fixture-', dir=parent))
        self.base = self.root / 'opt/usr/cc2-camera-reap-boot-r1'
        self.init = self.root / 'etc/init.d/printer'
        self.camera = self.root / 'opt/bin/ai_camera'
        self.source = self.root / 'package'
        self.stub = self.root / 'stubs'
        self.loader = self.root / 'lib/ld-linux-armhf.so.3'
        self.proc = self.root / 'proc/12345'
        self.source.mkdir()
        self.base.parent.mkdir(parents=True)
        self.proc.mkdir(parents=True)
        write(self.init, ORIGINAL.decode(), True)
        write(self.camera, '''#!/bin/sh
printf '%s|%s|%s\n' "${CC2_CAMERA_REAP_TRIAL-unset}" "${CC2_CAMERA_REAP_ENABLE_LAN-unset}" "${LD_PRELOAD-unset}" >> "$TEST_LAUNCHES"
''', True)
        write(self.loader, '''#!/bin/sh
case "${TEST_LOADER_FAIL:-0}" in 1) exit 127;; 2) echo 'libhv.so => not found'; exit 0;; esac
echo 'libhv.so => /opt/lib/libhv.so'
''', True)
        (self.proc / 'exe').symlink_to(self.camera)
        (self.proc / 'maps').write_text('fixture, no trial mapping\n')
        (self.proc / 'status').write_text('State:\tS (sleeping)\nVmSize:\t100 kB\nVmRSS:\t50 kB\nThreads:\t7\n')
        shutil.copyfile(noop, self.source / 'libcc2-camera-reap-r2.so')
        helper = '#!/bin/sh\n[ "${TEST_HELPER_FAIL:-0}" = 0 ] || exit 1\necho fixture-runtime-check\n'
        write(self.source / 'cc2-camera-reap-check', helper, True)
        substitutions = {
            'bcabb1fa405b066b06c229c6d8cc6e474a0ccc57e31a1b5b387d3b55945be563': HASH(ORIGINAL),
            'ec5633b17de329d5b18c3f8d8de6ebf389391a04193536d976ba167022d07b99': HASH(self.camera.read_bytes()),
            '8ef3a5c215462e3c9f32c3d4b85ea23e27c41e330d53b22d7cdeca9d100d016a': HASH(noop.read_bytes()),
            'dce1180e3c04512fea1c4f9868b131f3fbe9bda187ebc9342f9db633f0a57ea0': HASH(helper.encode()),
        }
        for name in NAMES:
            text = (HERE / name).read_text()
            for old, new in substitutions.items():
                text = text.replace(old, new)
            text = re.sub(r'/(?:opt|etc|lib|proc|tmp)/', lambda match: str(self.root) + match.group(0), text)
            write(self.source / name, text, True)
        manifest(self.source)
        write(self.stub / 'wget', '#!/bin/sh\nprintf \'%s\' "$TEST_STATE"\n', True)
        write(self.stub / 'id', '#!/bin/sh\necho 0\n', True)
        write(self.stub / 'pidof', '#!/bin/sh\n[ "${TEST_NO_CAMERA:-0}" = 0 ] || exit 1\necho 12345\n', True)
        write(self.stub / 'sync', '#!/bin/sh\nexit 0\n', True)
        self.launches = self.root / 'launches.txt'
        self.env = dict(os.environ, PATH=str(self.stub) + ':' + os.environ['PATH'],
                        TEST_STATE=IDLE, TEST_LAUNCHES=str(self.launches))
        for name in ('LD_PRELOAD', 'LD_LIBRARY_PATH', 'CC2_CAMERA_REAP_TRIAL', 'CC2_CAMERA_REAP_ENABLE_LAN'):
            self.env.pop(name, None)
        (self.root / 'tmp').mkdir()

    def run(self, action, expected=0, **env):
        global CASES
        CASES += 1
        result = subprocess.run(['sh', str(self.source / 'manage.sh'), action],
                                env=dict(self.env, **env), text=True, capture_output=True, timeout=15)
        assert (result.returncode == 0) == (expected == 0), (action, result)
        return result

    def boot(self, **env):
        global CASES
        CASES += 1
        result = subprocess.run(['sh', str(self.base / 'boot-camera.sh')],
                                env=dict(self.env, TEST_NO_CAMERA='1', **env), text=True, capture_output=True, timeout=15)
        assert result.returncode == 0, result
        return self.launches.read_text().splitlines()[-1].split('|')

    def unchanged(self):
        assert self.init.read_bytes() == ORIGINAL
        assert not self.base.exists()
        assert not self.launches.exists()

for name in NAMES:
    subprocess.run(['sh', '-n', str(HERE / name)], check=True)
assert 'CC2_CAMERA_REAP_ENABLE_LAN=0' in (HERE / 'boot-camera.sh').read_text()
assert 'CC2_CAMERA_REAP_ENABLE_LAN=1' not in (HERE / 'boot-camera.sh').read_text()
assert not any('kill ' in (HERE / name).read_text() or '\nreboot' in (HERE / name).read_text() for name in NAMES)

with tempfile.TemporaryDirectory(prefix='cc2-boot-tests-') as temporary:
    parent = Path(temporary)
    noop = parent / 'noop.so'
    subprocess.run(['gcc', '-shared', '-fPIC', '-x', 'c', '-', '-o', str(noop)], input='int fixture_noop;\n', text=True, check=True)
    f = Fixture(parent, noop)
    f.run('check'); f.unchanged()
    f.run('stage')
    assert not f.launches.exists(), 'staging must not restart the camera'
    assert (f.base / 'printer.original').read_bytes() == ORIGINAL
    assert stat.S_IMODE(f.init.stat().st_mode) == 0o755
    patched = f.init.read_text()
    assert patched.count('# CC2_CAMERA_BOOT_R1') == 1
    assert patched.index('unchanged-gui') < patched.index('# CC2_CAMERA_BOOT_R1') < patched.index('unchanged-mosquitto')
    f.run('stage', expected=1)
    first = f.boot()
    assert first == ['1', '0', str(f.base / 'libcc2-camera-reap-r2.so')], first
    assert not (f.base / 'armed').exists() and (f.base / 'consumed').exists()
    second = f.boot()
    assert second == ['0', '0', 'unset'], second
    f.run('rollback')
    assert f.init.read_bytes() == ORIGINAL

    f.run('rollback')
    assert f.init.read_bytes() == ORIGINAL

    # Status must bind the marker to the current PID and reject forced-LAN activation.
    f = Fixture(parent, noop); f.run('stage')
    (f.proc / 'maps').write_text(str(f.base / 'libcc2-camera-reap-r2.so') + '\n')
    marker = f.root / 'tmp/cc2-camera-reap.status'
    marker.write_text('trial=R2 pid=12345 patched=1 enable_lan_once=0 spawned=1 reaped=0\n')
    f.run('status')
    marker.write_text('trial=R2 pid=12345 patched=1 enable_lan_once=1 spawned=1 reaped=0\n')
    f.run('status', expected=1)
    marker.write_text('trial=R2 pid=99999 patched=1 enable_lan_once=0 spawned=1 reaped=0\n')
    f.run('status', expected=1)

    # The inserted startup line falls back to the native camera if /opt/usr payload is absent.
    f = Fixture(parent, noop); f.run('stage')
    shutil.move(f.base, f.root / 'payload-not-mounted')
    result = subprocess.run(['sh', str(f.init), 'start'], env=f.env, text=True, capture_output=True, timeout=15)
    assert result.returncode == 0 and f.launches.read_text().splitlines() == ['unset|unset|unset'], result

    # No duplicate camera on an already running service, even if a trial was armed.
    f = Fixture(parent, noop); f.run('stage')
    result = subprocess.run(['sh', str(f.base / 'boot-camera.sh')], env=f.env, text=True, capture_output=True, timeout=15)
    assert result.returncode != 0 and not f.launches.exists() and not (f.base / 'armed').exists()
    f.run('rollback')
    assert f.init.read_bytes() == ORIGINAL

    # All pre-install refusals preserve the original startup and never start a camera.
    for problem in ('wrong-init', 'wrong-camera', 'corrupt-payload', 'active-r2', 'busy', 'stale', 'disconnected', 'loader', 'missing-library', 'helper'):
        f = Fixture(parent, noop)
        env = {}
        if problem == 'wrong-init': f.init.write_bytes(ORIGINAL + b'# modified\n')
        elif problem == 'wrong-camera': f.camera.write_text('#!/bin/sh\nexit 0\n')
        elif problem == 'corrupt-payload': (f.source / 'libcc2-camera-reap-r2.so').write_bytes(b'broken')
        elif problem == 'active-r2': (f.proc / 'maps').write_text('libcc2-camera-reap-r2.so\n')
        elif problem == 'busy': env['TEST_STATE'] = IDLE.replace('"status":1', '"status":3')
        elif problem == 'stale': env['TEST_STATE'] = IDLE.replace('"age":2', '"age":99').replace('"last_message_age":2', '"last_message_age":99')
        elif problem == 'disconnected': env['TEST_STATE'] = IDLE.replace('"connected":true', '"connected":false')
        elif problem == 'loader': env['TEST_LOADER_FAIL'] = '1'
        elif problem == 'missing-library': env['TEST_LOADER_FAIL'] = '2'
        elif problem == 'helper': env['TEST_HELPER_FAIL'] = '1'
        before = f.init.read_bytes()
        f.run('stage', expected=1, **env)
        assert f.init.read_bytes() == before and not f.base.exists() and not f.launches.exists(), problem

    for problem in ('bad-payload', 'loader', 'helper', 'changed-startup'):
        f = Fixture(parent, noop); f.run('stage')
        env = {}
        if problem == 'bad-payload': (f.base / 'libcc2-camera-reap-r2.so').write_bytes(b'broken')
        elif problem == 'loader': env['TEST_LOADER_FAIL'] = '1'
        elif problem == 'helper': env['TEST_HELPER_FAIL'] = '1'
        elif problem == 'changed-startup': f.init.write_text(f.init.read_text() + '# later change\n')
        actual = f.boot(**env)
        assert actual == ['0', '0', 'unset'], (problem, actual)
        assert not (f.base / 'armed').exists(), problem

    f = Fixture(parent, noop); f.run('stage')
    f.init.write_text(f.init.read_text() + '# later firmware/user change\n')
    changed = f.init.read_bytes()
    f.run('rollback', expected=1)
    assert f.init.read_bytes() == changed
    f = Fixture(parent, noop); f.run('stage')
    (f.base / 'printer.original').write_text('wrong backup\n')
    changed = f.init.read_bytes()
    f.run('rollback', expected=1)
    assert f.init.read_bytes() == changed

print(f'PASS: {CASES} shell transaction/boot cases; exact backup/rollback, refusals, no service restart, launch order, one-shot consumption, native LAN flag retained and original fallback. No printer binary executed.')
