"""Actual disk runner on disposable files; no vendor service or printer runs."""
from pathlib import Path
import hashlib
import io
import os
import re
import shutil
import subprocess
import tarfile
import tempfile

ROOT = Path(__file__).resolve().parents[1]
TEMPLATE = (ROOT / 'installer/pc-stage.sh.in').read_text()

def prepare_fixture(shell, launcher, case):
    with tempfile.TemporaryDirectory(prefix='cc2-pc-prepare-') as directory:
        base = Path(directory); disk = base / 'opt/usr'; disk.mkdir(parents=True)
        proc = base / 'proc'; proc.mkdir()
        (proc / 'mounts').write_text(f'disk {disk} {"tmpfs" if case == "ram-mount" else "ext4"} rw 0 0\n')
        (proc / 'meminfo').write_text('MemAvailable: ' + ('8192' if case == 'low-memory' else '32768') + ' kB\n')
        stage = disk / ('cc2-control-install-' + 'a' * 32)
        if case == 'collision': stage.mkdir(); (stage / 'keep').write_text('keep')
        bins = base / 'bin'; bins.mkdir()
        (bins / 'id').write_text('#!/bin/sh\necho ' + ('1000' if case == 'not-root' else '0') + '\n')
        (bins / 'df').write_text('#!/bin/sh\necho "disk 100000 0 ' + ('1024' if case == 'low-disk' else '99999') + ' 0% disk"\n')
        for p in bins.iterdir(): p.chmod(0o755)
        text = (ROOT / 'installer' / launcher).read_text()
        if launcher.endswith('.ps1'):
            code = re.search(r"\$prepare = @'\n(.*?)\n'@", text, re.S)[1]
        else:
            code = re.search(r"<<'CC2_PREPARE'\n(.*?)\nCC2_PREPARE", text, re.S)[1]
        code = code.replace('/opt/usr', str(disk)).replace('/proc/mounts', str(proc / 'mounts')).replace('/proc/meminfo', str(proc / 'meminfo'))
        code = code.replace('__STAGE__', str(stage))
        code = code.replace('$(id -u)', '$("' + str(bins / 'id') + '" -u)').replace('$(df -k ', '$("' + str(bins / 'df') + '" -k ')
        file = base / 'prepare.sh'; file.write_text(code)
        result = subprocess.run([*shell, str(file), str(stage)], capture_output=True, text=True, timeout=10,
                                env=dict(os.environ, PATH=str(bins) + os.pathsep + os.environ['PATH']))
        assert (result.returncode == 0) == (case == 'success'), (launcher, shell, case, result.stderr)
        assert stage.exists() == (case in ('success', 'collision')), (launcher, case)
        if case == 'collision': assert (stage / 'keep').read_text() == 'keep'

def fixture(shell, case):
    with tempfile.TemporaryDirectory(prefix='cc2-pc-stage-') as directory:
        base = Path(directory)
        disk = base / 'opt/usr'
        stage = disk / ('cc2-control-install-' + 'a' * 32)
        stage.mkdir(parents=True)
        proc = base / 'proc'; proc.mkdir()
        (proc / 'mounts').write_text(f'disk {disk} {"tmpfs" if case == "ram-mount" else "ext4"} rw 0 0\n')
        (proc / 'meminfo').write_text('MemAvailable: ' + ('8192' if case == 'low-memory' else '32768') + ' kB\n')
        marker = base / 'installer-ran'
        script = ('#!/bin/sh\nprintf run > "' + str(marker) + '"\necho fixture-installer\nexit ' + ('7' if case == 'installer-failure' else '0') + '\n').encode()
        contents = {'install-on-printer.sh': script, 'ordinary-file': b'fixture'}
        contents['SHA256SUMS'] = ''.join(hashlib.sha256(data).hexdigest() + '  ' + name + '\n' for name, data in contents.items()).encode()
        buffer = io.BytesIO()
        with tarfile.open(fileobj=buffer, mode='w:gz') as archive:
            for name, data in contents.items():
                entry = tarfile.TarInfo(name); entry.size = len(data); entry.mode = 0o600
                archive.addfile(entry, io.BytesIO(data))
        payload = buffer.getvalue()
        archive_path = stage / 'cc2-control-payload.tar.gz'
        archive_path.write_bytes(payload)
        code = TEMPLATE.replace('@PAYLOAD_SHA@', hashlib.sha256(payload).hexdigest())
        # Redirect only fixed system paths to owned disposable fixtures.
        code = code.replace('/opt/usr', str(disk)).replace('/proc/mounts', str(proc / 'mounts')).replace('/proc/meminfo', str(proc / 'meminfo'))
        runner = stage / 'pc-stage.sh'; runner.write_text(code)
        bins = base / 'bin'; bins.mkdir()
        (bins / 'id').write_text('#!/bin/sh\nprintf "%s\\n" ' + ('1000' if case == 'not-root' else '0') + '\n')
        (bins / 'id').chmod(0o755)
        if case == 'extract-failure':
            (bins / 'tar').write_text('#!/bin/sh\nexit 9\n'); (bins / 'tar').chmod(0o755)
        # BusyBox may select internal applets ahead of PATH. These are the
        # fixture's process/filesystem boundaries, not production changes.
        code = code.replace('$(id -u)', '$("' + str(bins / 'id') + '" -u)')
        if case == 'extract-failure': code = code.replace('tar -xzf ', '"' + str(bins / 'tar') + '" -xzf ')
        runner.write_text(code)
        if case == 'changed-archive': archive_path.write_bytes(payload + b'changed')
        if case == 'payload-symlink':
            outside = base / 'outside'; outside.mkdir(); (outside / 'keep').write_text('keep')
            (stage / 'payload').symlink_to(outside)
        if case == 'log-symlink': (stage / 'install.log').symlink_to(base / 'outside-log')
        if case == 'previous-attempt': (stage / 'installer.exit').write_text('1\n')
        if case == 'stage-symlink':
            link = disk / ('cc2-control-install-' + 'b' * 32); link.symlink_to(stage); runner = link / runner.name
        result = subprocess.run([*shell, str(runner)], capture_output=True, text=True, timeout=15,
                                env=dict(os.environ, PATH=str(bins) + os.pathsep + os.environ['PATH']))
        assert (result.returncode == 0) == (case == 'success'), (shell, case, result.stdout, result.stderr)
        assert marker.exists() == (case in ('success', 'installer-failure')), (case, result.stdout, result.stderr)
        if case == 'success':
            assert not archive_path.exists() and not (stage / 'payload').exists()
            assert (stage / 'install.log').read_text().strip() == 'fixture-installer'
            assert (stage / 'installer.exit').read_text() == '0\n'
        else:
            assert archive_path.exists(), 'failed attempts must retain their payload'
        if case == 'installer-failure':
            assert (stage / 'payload').is_dir() and (stage / 'installer.exit').read_text() == '7\n'
        if case == 'payload-symlink': assert (base / 'outside/keep').read_text() == 'keep'
        if case == 'log-symlink': assert not (base / 'outside-log').exists()

for shell in (['dash'], ['busybox', 'sh']):
    assert shutil.which(shell[0]), 'Linux qualification requires dash and BusyBox'
    subprocess.run([*shell, '-n', str(ROOT / 'installer/install-cc2-control.sh')], check=True)
    for launcher in ('Install-CC2-Control.ps1', 'install-cc2-control.sh'):
        for case in ('success', 'not-root', 'ram-mount', 'low-memory', 'low-disk', 'collision'):
            prepare_fixture(shell, launcher, case)
    for case in ('success', 'not-root', 'ram-mount', 'low-memory', 'changed-archive', 'extract-failure',
                 'payload-symlink', 'log-symlink', 'previous-attempt', 'stage-symlink', 'installer-failure'):
        fixture(shell, case)
print('PASS: actual persistent PC runner on dash/BusyBox; low RAM, tmpfs, checksum/symlink/retry refusals, failure retention and success-only cleanup')
