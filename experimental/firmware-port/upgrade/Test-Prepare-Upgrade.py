"""Exercise the actual upgrade shell logic on temporary files, without SSH."""
from contextlib import ExitStack
import hashlib
import io
import os
from pathlib import Path
import re
import shlex
import shutil
import subprocess
import sys
import tarfile
from tempfile import TemporaryDirectory


SOURCE = Path(__file__).with_name('prepare-upgrade.sh').read_text(encoding='utf-8')
TARGETS = ('cc2-control-backup-20261110-065922-3209', 'cc2-control-install-' + 'a' * 32)
PRINTER_SHA = '19712b5943b10bb8cb6aab9a4ed498ee962b28538b23c91b7ed003eb9e590745'
DAEMON_SHA = '83433b14e49d1e8aa9523282ead03618378573f4b66de1ad8f7f3c444b11aada'


def run_case(shell, case):
    with TemporaryDirectory(prefix='cc2-upgrade-test-') as name:
        root = Path(name)
        data = root / 'opt/usr'
        proc = root / 'proc'
        for directory in (data, proc / '123/fd', root / 'tmp'):
            directory.mkdir(parents=True)
        (proc / 'mounts').touch()
        (proc / '123/maps').touch()
        for path in TARGETS + ('cc2-control', 'cc2-control-install-foreign'):
            (data / path).mkdir()
            (data / path / 'contents').write_text(path, encoding='ascii')
        (data / ('cc2-control-install-' + 'b' * 32)).symlink_to(data / 'cc2-control')
        personal = ('wifi/wpa_supplicant.conf', 'cfg/autosave.cfg',
                    'gcodes/keep.gcode', 'cc2-control/settings.json')
        for relative in personal:
            file = data / relative
            file.parent.mkdir(parents=True, exist_ok=True)
            file.write_bytes(('preserve:' + relative).encode())

        probe = root / 'tmp/probe'
        probe.write_text('#!/bin/sh\n[ "$1" = --installer-native-idle ] && [ ! -e "$FIXTURE_BUSY" ]\n',
                         encoding='ascii')
        digest = hashlib.sha256(probe.read_bytes()).hexdigest()
        idle = root / 'busy'
        env = {**os.environ, 'FIXTURE_BUSY': str(idle)}
        evidence = root / 'tmp/cc2-upgrade-evidence-fixture'
        evidence.write_text('#!/bin/sh\n[ "$1" = --inspect ] && [ "$FIXTURE_EVIDENCE" != INVALID ] || exit 1\nprintf "%s\\n" "$FIXTURE_EVIDENCE"\n')
        evidence_digest = hashlib.sha256(evidence.read_bytes()).hexdigest()
        env['FIXTURE_EVIDENCE'] = 'ABSENT'
        shell_source = re.sub(r'/opt/(?:usr|bin|inst)|/etc/init\.d|/proc|/tmp',
                              lambda match: str(root / match[0].lstrip('/')), SOURCE)
        # Fixture files belong to the WSL/CI user. Production still requires root.
        shell_source = shell_source.replace('&&$3==0)', f'&&$3=={os.getuid()})')
        wrappers = f"""id() {{ printf '0\\n'; }}
pidof() {{ printf '123\\n'; }}
sha256sum() {{
    case "${{1:-}}" in
        '{root}/opt/bin/elegoo_printer'|'{proc}/123/exe')
            printf '%s  %s\\n' '{PRINTER_SHA}' "$1";;
        '{root}/opt/inst/daemon-000/daemon-000')
            printf '%s  %s\\n' '{DAEMON_SHA}' "$1";;
        *) command sha256sum "$@";;
    esac
}}
"""
        if shell[-1] == 'ash':
            wrappers += 'tar() { ' + shlex.join(shell[:-1]) + ' tar "$@"; }\n'
        targets = TARGETS
        if case in ('marker', 'marker-changed', 'marker-link', 'marker-hardlink'):
            targets += ('cc2-reactor-disabled',)
            (data / 'cc2-reactor-disabled').touch()
        if case == 'dormant':
            targets += ('cc2-rfid',)
            (data / 'cc2-rfid').mkdir()
            (data / 'cc2-rfid/contents').write_text('dormant evidence fixture')
            env['FIXTURE_EVIDENCE'] = 'DORMANT'
        if case in ('restored', 'restored-changed', 'active'):
            env['FIXTURE_EVIDENCE'] = 'RESTORED'
            targets += ('cc2-rfid',)
            startup_files = [root / 'etc/init.d/cc2-reactor-launch',
                             *[data / ('cc2-reactor-runtime-v2/' + leaf) for leaf in ('launcher.sh', 'SHA256SUMS')],
                             *[data / ('cc2-network-manager/' + leaf) for leaf in
                               ('profile.sh', 'reactor-entry.sh', 'activate.sh', 'SHA256SUMS', 'startup.sha256')]]
            for file in startup_files:
                file.parent.mkdir(parents=True, exist_ok=True)
                file.write_bytes(b'stock startup fixture')
            stock_hash = hashlib.sha256(b'stock startup fixture').hexdigest()
            rfid = data / 'cc2-rfid'
            (rfid / 'startup-tx').mkdir(parents=True)
            for file in (data / 'cc2-control/cc2-control', rfid / 'app-integration.json',
                         rfid / 'recovery-profile', rfid / 'original-stock-44.bin',
                         rfid / 'lifecycle', rfid / 'enabled', rfid / 'startup-tx/restored'):
                file.write_bytes(b'qualified fixture')
            (rfid / 'startup-plan').write_text('header\ndispatch\nstock-entry\n' +
                ''.join(f'{i} 0700 0700 {stock_hash} unused\n' for i in range(8)))

        def run(action, checksum=''):
            args = ' '.join(map(shlex.quote, targets))
            prefix = f'action={action}; probe={shlex.quote(str(probe))}; probe_sha={digest}; archive_sha={checksum}\nset -- {args}\n'
            prefix += f'evidence={shlex.quote(str(evidence))}; evidence_sha={evidence_digest}; evidence_keep=1\n'
            return subprocess.run(shell + ['-s'], input=(prefix + wrappers + shell_source).encode(),
                                  capture_output=True, env=env, timeout=15)

        plan = run('discover')
        assert plan.returncode == 0 and plan.stdout.decode().splitlines() == [*targets, 'PLAN COMPLETE'], plan
        backup = run('backup')
        assert backup.returncode == 0, backup
        with tarfile.open(fileobj=io.BytesIO(backup.stdout), mode='r:') as archive:
            assert {entry.name.split('/')[0] for entry in archive} == set(targets)
        checksum = hashlib.sha256(backup.stdout).hexdigest()
        expected_error = {
            'changed': b'targets_changed_since_backup',
            'busy': b'printer_not_idle_cold',
            'canvas': b'review_pending_state',
            'in-use': b'target_in_use',
            'mounted': b'mounted_target',
            'symlink': b'target_changed',
            'locked': b'installer_busy',
            'restored-changed': b'canvas_evidence_not_verified',
            'active': b'canvas_evidence_not_verified',
            'marker-changed': b'invalid_reactor_disable_marker',
            'marker-link': b'invalid_reactor_disable_marker',
            'marker-hardlink': b'unsafe_reactor_disable_marker',
            'evidence-changed': b'canvas_evidence_probe_changed',
            'evidence-unknown': b'invalid_canvas_evidence_reply',
        }.get(case)
        if case == 'changed':
            (data / TARGETS[0] / 'contents').write_text('Changed after backup', encoding='ascii')
        elif case == 'busy':
            idle.touch()
        elif case == 'canvas':
            (data / 'cc2-rfid').mkdir()
            (data / 'cc2-rfid/lifecycle.next').touch()
        elif case == 'in-use':
            (proc / '123/fd/9').symlink_to(data / TARGETS[0] / 'contents')
        elif case == 'mounted':
            (proc / 'mounts').write_text(f'/dev/fake {data / TARGETS[0]} ext4 rw 0 0\n')
        elif case == 'symlink':
            shutil.rmtree(data / TARGETS[0])
            (data / TARGETS[0]).symlink_to(data / 'cc2-control')
        elif case == 'locked':
            (root / 'tmp/cc2-control-install.lock').mkdir()
        elif case == 'restored-changed':
            startup_files[0].write_bytes(b'changed startup')
            env['FIXTURE_EVIDENCE'] = 'INVALID'
        elif case == 'active':
            env['FIXTURE_EVIDENCE'] = 'INVALID'
        elif case == 'evidence-changed':
            evidence.write_text('changed probe')
        elif case == 'evidence-unknown':
            env['FIXTURE_EVIDENCE'] = 'FOREIGN'
        elif case == 'marker-changed':
            (data / 'cc2-reactor-disabled').write_bytes(b'unknown marker')
        elif case == 'marker-link':
            (data / 'cc2-reactor-disabled').unlink()
            (data / 'cc2-reactor-disabled').symlink_to(data / 'cc2-control/contents')
        elif case == 'marker-hardlink':
            os.link(data / 'cc2-reactor-disabled', data / 'other-marker-link')

        result = run('apply', checksum)
        quarantines = list(data.glob('.cc2-upgrade-quarantine.*'))
        if expected_error:
            assert result.returncode and expected_error in result.stderr, result
            assert not quarantines and all((data / path).exists() for path in targets)
        else:
            assert result.returncode == 0 and b'QUARANTINE COMPLETE' in result.stdout, result
            assert len(quarantines) == 1 and not any((data / path).exists() for path in targets)
            for path in TARGETS:
                assert (quarantines[0] / path / 'contents').read_text(encoding='ascii') == path
            if case == 'restored':
                assert (quarantines[0] / 'cc2-rfid/original-stock-44.bin').is_file()
            if case == 'marker':
                assert (quarantines[0] / 'cc2-reactor-disabled').read_bytes() == b''
        assert (data / 'cc2-control/contents').read_text(encoding='ascii') == 'cc2-control'
        assert (data / 'cc2-control-install-foreign/contents').is_file()
        for relative in personal:
            assert (data / relative).read_bytes() == ('preserve:' + relative).encode()
        assert (root / 'tmp/cc2-control-install.lock').is_dir() == (case == 'locked')
        print(f'PASS {shell[-1]} {case}')


def stock_busybox(stack):
    firmware = Path(__file__).resolve().parents[2] / 'work/cc2-stock-02.01.01.07/rootfs.squashfs'
    with firmware.open('rb') as source:
        if hashlib.file_digest(source, 'sha256').hexdigest() != '20375a79e1ee2748f43fd39a62e5a48b23b945ed048dbe52a54d0a1f37b1acae':
            raise SystemExit('Unexpected .07 RootFS checksum')
    if not shutil.which('unsquashfs'):
        raise SystemExit('unsquashfs is required in WSL')
    emulator = shutil.which('qemu-arm') or shutil.which('qemu-arm-static')
    if not emulator:
        raise SystemExit('Neither qemu-arm nor qemu-arm-static is available in WSL')
    root = Path(stack.enter_context(TemporaryDirectory(prefix='cc2-stock-busybox-'))) / 'rootfs'
    subprocess.run(['unsquashfs', '-no-progress', '-processors', '2', '-d', root,
                    firmware, 'bin/busybox', 'lib', 'usr/lib'],
                   check=True, timeout=120)
    executable = root / 'bin/busybox'
    if not executable.is_file() or executable.is_symlink():
        raise SystemExit('The stock ARM BusyBox executable was not extracted')
    prefix = [emulator, '-L', str(root), str(executable)]
    subprocess.run(prefix + ['ash', '-c', 'exit 0'], check=True, timeout=5)
    return prefix


def check(shell):
    subprocess.run(shell + ['-n'], input=SOURCE.encode(), check=True, timeout=5)
    for case in ('success', 'changed', 'busy', 'canvas', 'in-use', 'mounted', 'symlink', 'locked',
                 'restored', 'restored-changed', 'active', 'marker', 'marker-changed',
                 'marker-link', 'marker-hardlink', 'dormant', 'evidence-changed', 'evidence-unknown'):
        run_case(shell, case)


if __name__ == '__main__':
    if '--stock-busybox' in sys.argv:
        with ExitStack() as stack:
            check(stock_busybox(stack) + ['ash'])
        print('PASS OFFLINE STOCK BUSYBOX PREPARE-UPGRADE')
        sys.exit(0)
    if '--require-busybox' in sys.argv and not shutil.which('busybox'):
        raise SystemExit('BusyBox is required for this qualification')
    shells = [['bash']]
    if shutil.which('busybox'):
        shells.append(['busybox', 'ash'])
    for shell in shells:
        check(shell)
    if len(shells) == 1:
        print('SKIP BusyBox ash: busybox is unavailable')
    print('PASS OFFLINE PREPARE-UPGRADE')
