#!/usr/bin/env python3
"""The installer writes the gzip copies of the start page and the locale files, and an update never fails on them.

installer/install-on-printer.sh first removes every index.html.gz and locales/*.json.gz (and any half-written
*.gz.new), then writes one copy per installed file through <file>.gz.new and mv. Without gzip, when gzip fails or
the disk is full, no copy and no *.gz.new is left and the installation still passes. The sequence install ->
older installer (no copies) -> restore -> install again is served by the real service after every step: the copy
is used only while it is the gzip form of the installed file, a copy left by another version never is. A power loss
while packing leaves at most a *.gz.new, which the next installation removes.
"""
import gzip
import hashlib
import http.client
import importlib.util
import io
import os
import pathlib
import shlex
import shutil
import socket
import subprocess
import sys
import tarfile
import tempfile
import time
import zipfile

ROOT = pathlib.Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location('updater', ROOT / 'installer/package.py')
pkg = importlib.util.module_from_spec(spec)
spec.loader.exec_module(pkg)
binary = pathlib.Path(sys.argv[1]).resolve()
runner = shlex.split(os.environ.get('CC2_TEST_RUNNER', ''))
GZIP_CALL = 'web_gzip_copies 2>/dev/null || true\n'
LOCALES = ('de', 'en', 'ru')


def page(label):
    return ('<!doctype html><html><body>' + (label + ' cc2 control page ') * 3000 + '</body></html>\n').encode()


def locale(code, label):
    return ('{"common.ok":"' + (code + ' ' + label + ' ') * 2000 + '"}\n').encode()


def build_package(base, label):
    """A package as installer/package.py writes it, with this checkout's installer and the given web files."""
    prepared = base / ('prepared-' + label)
    (prepared / 'web/locales').mkdir(parents=True)
    arm = bytearray(64); arm[:6] = b'\x7fELF\x01\x01'; arm[18] = 40
    for name in ('cc2-control', 'cc2-https-download', 'cc2-update-worker'):
        (prepared / name).write_bytes(bytes(arm))
    (prepared / 'web/index.html').write_bytes(page(label))
    for code in LOCALES:
        (prepared / 'web/locales' / (code + '.json')).write_bytes(locale(code, label))
    (prepared / 'ca-bundle.pem').write_bytes((ROOT / 'updater/ca-bundle.pem').read_bytes())
    (prepared / 'update-runner.sh').write_bytes((ROOT / 'scripts/update-runner.sh').read_bytes())
    (prepared / 'updater-trusted-key.json').write_bytes((ROOT / 'updater/trusted-key.json').read_bytes())
    (prepared / 'THIRD_PARTY_NOTICES.txt').write_bytes((ROOT / 'third-party/monocypher/LICENCE.md').read_bytes())
    source = base / ('source-' + label + '.zip'); source.write_bytes(b'test source')
    output = base / ('updater-' + label + '.zip')
    pkg.package(prepared, ROOT / 'scripts', source, output)
    with zipfile.ZipFile(output) as z:
        payload = next(z.read(n) for n in z.namelist() if n.endswith('cc2-control-payload.tar.gz'))
    with tarfile.open(fileobj=io.BytesIO(payload), mode='r:gz') as t:
        names = [m.name for m in t.getmembers()]
    assert not any(n.endswith('.gz') for n in names), 'The package must carry no gzip copy: ' + repr(names)
    return payload, bytes(arm)


class Printer:
    """A printer fixture: target directory, init script, vendor fingerprints and stand-in commands."""

    def __init__(self, base):
        self.base = base
        self.target = base / 'opt/usr/cc2-control'
        self.target.mkdir(parents=True)
        (self.target / 'cc2-control').write_bytes(b'OLD')
        (self.target / 'cc2-control.conf').write_text('mqtt_username=elegoo\nmqtt_password=gzip\n')
        self.init = base / 'init'; self.marker = base / 'running'
        self.init.write_text('#!/bin/sh\ncase "$1" in start) touch "' + str(self.marker) + '";; stop) rm -f "' + str(self.marker) + '";; esac\n')
        self.init.chmod(0o755)
        self.printer = base / 'vendor-printer'; self.printer.write_bytes(b'vendor-printer')
        self.daemon = base / 'vendor-daemon'; self.daemon.write_bytes(b'vendor-daemon')
        self.commands = base / 'bin'; self.commands.mkdir()
        status = ('{"connected":true,"machine":{"status":1,"x":0},"last_message_age":0,'
                  '"extruder":{"temperature":24,"target":0},"heater_bed":{"temperature":24,"target":0},"x":0}')
        (self.commands / 'wget').write_text('#!/bin/sh\ncase "$*" in *api/printer*) echo \'' + status + '\';; *) echo \'{"service":"cc2-control","version":"' + pkg.VERSION + '","mqtt_registered":true,"snapshot_received":true}\';; esac\n')
        (self.commands / 'pidof').write_text('#!/bin/sh\nif [ "$1" = elegoo_printer ]; then echo 2024; exit 0; fi\nif [ -f "' + str(self.marker) + '" ]; then echo 999; else exit 1; fi\n')
        (self.commands / 'sleep').write_text('#!/bin/sh\n[ "$1" != 2 ] || exit 0\nexec /bin/sleep "$@"\n')
        for p in self.commands.iterdir(): p.chmod(0o755)
        (base / 'meminfo').write_text('MemAvailable: 65536 kB\n')
        self.runs = 0

    def localise(self, text):
        text = text.replace('/opt/usr/cc2-control', str(self.target)).replace('/etc/init.d/cc2-control', str(self.init))
        text = text.replace('/tmp/cc2-control-install.lock', str(self.base / 'lock')).replace('/proc/$RUN_PID/exe', str(self.target / 'cc2-control'))
        text = text.replace('/opt/bin/elegoo_printer', str(self.printer)).replace('/opt/inst/daemon-000/daemon-000', str(self.daemon))
        text = text.replace('/proc/$printer_pid/exe', str(self.printer)).replace('/proc/meminfo', str(self.base / 'meminfo'))
        text = text.replace(pkg.COMPATIBILITY['printer_sha256'], hashlib.sha256(b'vendor-printer').hexdigest())
        return text.replace(pkg.COMPATIBILITY['daemon_sha256'], hashlib.sha256(b'vendor-daemon').hexdigest())

    def install(self, payload, older_installer=False, path=None, passes=True):
        """Unpacks the payload like the update runner and runs its installer; returns the finished process."""
        self.runs += 1
        stage = self.base / ('stage-' + str(self.runs)); stage.mkdir()
        with tarfile.open(fileobj=io.BytesIO(payload), mode='r:gz') as t:
            for m in t.getmembers():
                p = stage / m.name; p.parent.mkdir(parents=True, exist_ok=True); p.write_bytes(t.extractfile(m).read()); p.chmod(m.mode)
        (stage / 'cc2-control.init').write_bytes(self.init.read_bytes())
        (stage / 'SHA256SUMS').write_text(''.join(hashlib.sha256(p.read_bytes()).hexdigest() + '  ' + p.relative_to(stage).as_posix() + '\n'
                                                   for p in sorted(stage.rglob('*')) if p.is_file() and p.name != 'SHA256SUMS'))
        script = self.localise((stage / 'install-on-printer.sh').read_text())
        script = script.replace('STAGE=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)', 'STAGE="' + str(stage) + '"')
        script = script.replace('"$STAGE/cc2-control" --installer-native-idle', ':')
        assert script.count(GZIP_CALL) == 1, 'installer no longer calls web_gzip_copies once'
        if older_installer:
            # The installers of every release before this one: the same steps without the copies.
            script = script.replace(GZIP_CALL, '')
        run = self.base / ('run-' + str(self.runs) + '.sh'); run.write_text(script)
        result = subprocess.run(['sh', str(run)], env=dict(os.environ, PATH=path or (str(self.commands) + ':' + os.environ['PATH'])),
                                capture_output=True, text=True, timeout=120)
        if not passes: return result
        assert result.returncode == 0 and 'INSTALLATION PASSED' in result.stdout, result.stdout + result.stderr
        assert not (self.base / 'lock').exists()
        self.backup = pathlib.Path(result.stdout.split('INSTALLATION PASSED. Backup: ', 1)[1].split()[0])
        return result

    def restore(self):
        backup = self.backup  # the one taken by the last installation
        script = self.localise((backup / 'restore.sh').read_text())
        (backup / 'restore.sh').write_text(script)
        result = subprocess.run(['sh', str(backup / 'restore.sh')], env=dict(os.environ, PATH=str(self.commands) + ':' + os.environ['PATH']),
                                capture_output=True, text=True, timeout=30)
        assert result.returncode == 0, result.stdout + result.stderr

    def web_files(self):
        web = self.target / 'web'
        return [web / 'index.html'] + sorted((web / 'locales').glob('*.json'))

    def leftovers(self):
        return sorted(str(p.relative_to(self.target)) for p in (self.target / 'web').rglob('*.gz.new'))

    def copies(self):
        return sorted(str(p.relative_to(self.target)) for p in (self.target / 'web').rglob('*.gz'))

    def expect_copies(self, what):
        for plain in self.web_files():
            copy = plain.with_name(plain.name + '.gz')
            assert copy.is_file(), what + ': no copy of ' + plain.name
            assert gzip.decompress(copy.read_bytes()) == plain.read_bytes(), what + ': copy of ' + plain.name + ' is not its gzip form'
            assert copy.stat().st_mode & 0o777 == 0o644, what + ': mode of ' + copy.name
        assert len(self.copies()) == len(self.web_files()), what + ': ' + repr(self.copies())
        assert self.leftovers() == [], what + ': ' + repr(self.leftovers())

    def expect_no_copies(self, what):
        assert self.copies() == [] and self.leftovers() == [], what + ': ' + repr(self.copies() + self.leftovers())


def free_port():
    with socket.socket() as probe:
        probe.bind(('127.0.0.1', 0))
        return probe.getsockname()[1]


def fetch(port, path, gzip_wanted):
    connection = http.client.HTTPConnection('127.0.0.1', port, timeout=5)
    try:
        connection.putrequest('GET', path, skip_accept_encoding=True)
        if gzip_wanted: connection.putheader('Accept-Encoding', 'gzip')
        connection.endheaders()
        response = connection.getresponse()
        return response.status, response.getheader('Content-Encoding'), response.read()
    finally:
        connection.close()


def served(printer, what, label, packed):
    """Runs the real service on the installed web files: the page and every locale arrive as `label`'s bytes."""
    state = printer.base / ('service-' + str(free_port()))
    state.mkdir()
    port = free_port()
    process = subprocess.Popen([*runner, str(binary), '--port', str(port), '--panda-port', '0', '--web-root', str(printer.target / 'web'),
                                '--config', str(printer.target / 'cc2-control.conf'), '--presets', str(state / 'presets.json'),
                                '--preferences', str(state / 'preferences.json')], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    try:
        for _ in range(300):
            try:
                fetch(port, '/', False)
                break
            except OSError:
                assert process.poll() is None, f'cc2-control exited with {process.returncode}'
                time.sleep(0.1)
        else:
            raise AssertionError('cc2-control did not start listening')
        expected = [('/', page(label))] + [('/i18n/' + code + '.json', locale(code, label)) for code in LOCALES]
        for path, plain in expected:
            status, encoding, data = fetch(port, path, True)
            body = gzip.decompress(data) if encoding == 'gzip' else data
            assert status == 200 and body == plain, f'{what}: {path} with gzip is not the installed file ({status}, {encoding})'
            assert encoding == ('gzip' if packed else None), f'{what}: {path} sent with Content-Encoding {encoding!r}'
            status, encoding, data = fetch(port, path, False)
            assert status == 200 and encoding is None and data == plain, f'{what}: {path} without gzip'
    finally:
        process.terminate()
        try:
            process.wait(timeout=5)
        except subprocess.TimeoutExpired:
            process.kill()
            process.wait()


def path_without(commands, name):
    """PATH with the fixture commands and every other program, but no `name`."""
    hidden = commands.parent / ('path-without-' + name)
    hidden.mkdir()
    for directory in [str(commands)] + os.environ['PATH'].split(os.pathsep):
        if not os.path.isdir(directory): continue
        for entry in os.listdir(directory):
            source = os.path.join(directory, entry)
            if entry == name or (hidden / entry).exists() or not os.access(source, os.X_OK) or os.path.isdir(source): continue
            (hidden / entry).symlink_to(source)
    assert shutil.which(name, path=str(hidden)) is None
    return str(hidden)


with tempfile.TemporaryDirectory(prefix='cc2-install-gzip-') as temporary:
    base = pathlib.Path(temporary)
    new, _ = build_package(base, 'new')
    older, _ = build_package(base, 'older')

    # Upgrade over an installation from before the copies (two releases back or more, no copy at all), with
    # a stale copy and half-written copies left over: every copy is the gzip form of the installed file.
    printer = Printer(base / 'skip')
    web = printer.target / 'web'; (web / 'locales').mkdir(parents=True)
    (web / 'index.html').write_bytes(page('ancient'))
    (web / 'index.html.gz').write_bytes(gzip.compress(page('ancient')))
    (web / 'index.html.gz.new').write_bytes(b'\x1f\x8b\x08 cut off')
    (web / 'locales/de.json.gz.new').write_bytes(b'')
    printer.install(new)
    printer.expect_copies('upgrade')
    served(printer, 'upgrade', 'new', packed=True)

    # Downgrade with an older package: its installer replaces the files but not the copies.
    printer.install(older, older_installer=True)
    assert len(printer.copies()) == 4, printer.copies()
    served(printer, 'downgrade by an older package', 'older', packed=False)

    # Upgrade again: the stale copies are replaced.
    printer.install(new)
    printer.expect_copies('upgrade after downgrade')
    served(printer, 'upgrade after downgrade', 'new', packed=True)

    # Restore the backup of that update (the older files, the stale copies of the new ones).
    printer.restore()
    assert (printer.target / 'web/index.html').read_bytes() == page('older')
    served(printer, 'restore', 'older', packed=False)

    # Without gzip: no copy, the old ones removed, the installation passes.
    printer = Printer(base / 'no-gzip')
    printer.install(new)
    printer.install(new, path=path_without(printer.commands, 'gzip'))
    printer.expect_no_copies('no gzip')
    served(printer, 'no gzip', 'new', packed=False)

    # gzip fails after a part of the copy, or the disk is full: no copy, no half copy, the installation passes.
    for case, body in (('gzip fails', 'echo "gzip: invalid option" >&2; exit 1'),
                       ('disk full', 'head -c 100 "$2"; echo "gzip: write error: No space left on device" >&2; exit 1')):
        printer = Printer(base / case.replace(' ', '-'))
        printer.install(new)
        (printer.commands / 'gzip').write_text('#!/bin/sh\n' + body + '\n')
        (printer.commands / 'gzip').chmod(0o755)
        printer.install(new)
        printer.expect_no_copies(case)
        served(printer, case, 'new', packed=False)

    # Power lost while the page is packed (the installer dies with gzip): a copy is never left half-written under
    # its own name, and the next installation removes the half-written *.gz.new.
    printer = Printer(base / 'power-loss')
    real_gzip = shutil.which('gzip')
    (printer.commands / 'gzip').write_text('#!/bin/sh\ncase "$2" in *index.html) "' + real_gzip + '" "$@" | head -c 100; kill -KILL $PPID; exit 1;; esac\nexec "' + real_gzip + '" "$@"\n')
    (printer.commands / 'gzip').chmod(0o755)
    result = printer.install(new, passes=False)
    assert result.returncode == -9, result.stdout + result.stderr
    assert not (printer.target / 'web/index.html.gz').exists() and printer.leftovers() == ['web/index.html.gz.new'], printer.copies() + printer.leftovers()
    (printer.base / 'lock').rmdir()  # /tmp does not survive the power loss
    (printer.commands / 'gzip').unlink()
    printer.install(new)
    printer.expect_copies('after a power loss')

    # Full disk for one file only (the page): the locales keep their copies, the page is sent plain.
    printer = Printer(base / 'disk-full-page')
    (printer.commands / 'gzip').write_text('#!/bin/sh\ncase "$2" in *index.html) head -c 100 "$2"; exit 1;; esac\nexec "' + real_gzip + '" "$@"\n')
    (printer.commands / 'gzip').chmod(0o755)
    printer.install(new)
    assert printer.copies() == sorted('web/locales/' + code + '.json.gz' for code in LOCALES), printer.copies()
    assert printer.leftovers() == []
print('PASS: installer gzip copies on upgrade over a pre-copy installation, downgrade by an older package, upgrade again and restore '
      '(served by the service after each), without gzip, on a gzip error, on a full disk and after a power loss')
