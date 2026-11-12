"""Stage the exact .13 RootFS on Linux and audit the combined boot files offline."""

import argparse
from contextlib import nullcontext
import hashlib
import io
import json
import os
from pathlib import Path
import re
import shlex
import shutil
import subprocess
import sys
import tarfile
from tempfile import TemporaryDirectory

PORT = Path(__file__).resolve().parent
sys.path.insert(0, str(PORT / 'source/builder/current/core'))

from firmware_builder import cc2_control_files, reqhash
from stage_stock13 import PRINTER_PULLBACK13_SHA256, stage
from startup_stock13 import PRINTER13_EXTENSIONS, canvas_recovery_profile13

ROOTFS_SHA = '30b63303eb13175ee0a2c65e3b94dd233cb148fe3cc68b6c1ddebca044650910'
ARCHIVE = 'CC2-Connections-Runtime-LAN-322-323-8c7c6a93ed64.tar.gz'


def prepare_test_manager(scratch, qualified):
    """Rebuild the .13 manager against the resident CC2 Control path."""
    receipt = json.loads((qualified / 'native13-check.json').read_bytes())
    for name, digest in {**receipt['modules'], 'control': receipt['manager_sha256'],
                         'camera-recovery-test': receipt['test_sha256']}.items():
        reqhash(qualified / name, digest, '.13 qualified input ' + name)
    target = scratch / 'connections'
    shutil.copytree(qualified, target)
    compiler = '/opt/cc2-cross/toolchain-out/bin/arm-cortex_a15-linux-gnueabihf-gcc'
    subprocess.run([compiler, '-O2', '-std=c11', '-Wall', '-Wextra', '-Wpedantic',
                    '-Werror', '-static', '-s', '-mcpu=cortex-a7', '-mfpu=neon-vfpv4',
                    '-mfloat-abi=hard', '-pthread', '-DCC2_NATIVE_STOCK13',
                    f'-DNG_PRINTER_INIT_SHA="{receipt["printer_init_sha256"]}"',
                    f'-DNG_LIBCC2_NETWORK_COMPAT_R1_SO_SHA="{receipt["modules"]["libcc2-network-compat-r1.so"]}"',
                    PORT / 'source/experimental/network-connections/manager.c',
                    '-o', target / 'control', '-lm'], check=True, timeout=120)
    receipt['manager_sha256'] = hashlib.sha256((target / 'control').read_bytes()).hexdigest()
    (target / 'native13-check.json').write_text(json.dumps(receipt, indent=2) + '\n')
    return target, receipt['manager_sha256']


def prepare_test_app(scratch, manager_sha=None):
    """Build this port's live source in scratch without changing release pins."""
    builder = PORT / 'source/builder/current'
    sys.path.append(str(builder))
    from prepare import copy_shell_lf, prepared_mode, sha256, validate_arm_elf

    source = PORT / 'source/cc2-control'
    tree = scratch / 'app-source'
    tree.mkdir()
    for name in ('src', 'updater', 'third-party', 'scripts', 'config', 'web'):
        shutil.copytree(source / name, tree / name)
    for name in ('Makefile', 'VERSION', 'FIRMWARE_VERSION'):
        shutil.copyfile(source / name, tree / name)
    if manager_sha is not None:
        pin = tree / 'src/connections/pin.h'
        updated, count = re.subn(r'(?m)^#define CC2_CONNECTIONS_STOCK13_HELPER_SHA "[0-9a-f]{64}"$',
                                 f'#define CC2_CONNECTIONS_STOCK13_HELPER_SHA "{manager_sha}"',
                                 pin.read_text())
        if count != 1:
            raise RuntimeError('Unexpected .13 Connections manager pin')
        pin.write_text(updated)
    prefix = '/opt/cc2-cross/toolchain-out/bin/arm-cortex_a15-linux-gnueabihf-'
    if not Path(prefix + 'gcc').is_file():
        raise RuntimeError('Missing qualified ARM toolchain used for Reactor')
    subprocess.run(['make', 'dist', 'CROSS=' + prefix, 'CC=' + prefix + 'gcc',
                    'AR=' + prefix + 'ar',
                    'EXTRA_CFLAGS=-mcpu=cortex-a7 -mfpu=neon-vfpv4 -mfloat-abi=hard -DCC2_RFID_NATIVE_SCAN=1'],
                   cwd=tree, check=True, timeout=600)
    dist = tree / 'dist/cc2-control'
    component = scratch / 'app-prepared'
    shutil.copytree(dist, component, ignore=shutil.ignore_patterns('cc2-control.conf.example'))
    runtime = builder / 'components/cc2-control/runtime'
    for name in ('start.sh', 'launch.sh', 'cc2-control.init', 'cc2-configure'):
        copy_shell_lf(runtime / name, component / name)
    defaults = component / 'defaults'
    defaults.mkdir()
    shutil.copyfile(source / 'firmware-integration/overlay/opt/inst/cc2-control/defaults/material-presets.json',
                    defaults / 'material-presets.json')
    version = (tree / 'VERSION').read_text().strip()
    for name in ('cc2-control', 'cc2-update-worker', 'cc2-https-download'):
        validate_arm_elf(component / name, require_version=name == 'cc2-control', version=version)
    files = {}
    for path in sorted(component.rglob('*')):
        if path.is_file():
            name = path.relative_to(component).as_posix()
            files[name] = {'sha256': sha256(path), 'mode': oct(prepared_mode(name))}
    if set(files) != set(cc2_control_files(files)):
        raise RuntimeError('Unexpected local CC2 Control payload file set')
    print('PASS: local CC2 Control ARM app and updater components built')
    return component, {'component': 'CC2 Control', 'version': version, 'files': files}


def find_extensions(folder):
    for path in sorted(folder.glob('*/native13-check.json')):
        try:
            receipt = json.loads(path.read_bytes())
            modules = receipt.get('modules', {})
            if (receipt.get('printer_sha256') == PRINTER_PULLBACK13_SHA256
                    and set(modules) == set(PRINTER13_EXTENSIONS)
                    and all(not (path.parent / name).is_symlink()
                            and hashlib.sha256((path.parent / name).read_bytes()).hexdigest() == digest
                            for name, digest in modules.items())):
                return path.parent
        except (OSError, ValueError):
            continue
    return None

def check_network_preload(root, receipt, scratch):
    """Exercise the generated helper with inert executables and files."""
    helper = (root / 'opt/inst/cc2-connections/native-preload.sh').read_text()
    camera = (root / 'etc/init.d/cc2-camera-launch').read_text()
    sequence = [camera.find(step) for step in (
        '. /opt/inst/cc2-connections/native-preload.sh',
        '"$network_control" --wait-camera-link || true',
        'exec "$camera" "$@"',
    )]
    if sequence[0] < 0 or not sequence[0] < sequence[1] < sequence[2]:
        raise RuntimeError('Camera must prepare Ethernet and wait for carrier before launch (#322)')
    control = scratch / 'control'
    library = scratch / 'network.so'
    log = scratch / 'commands'
    control.write_text('#!/bin/sh\nprintf "%s\\n" "$1" >> "$CC2_TEST_LOG"\n'
                       'if [ "$1" = --enabled ]; then [ "$CC2_TEST_ENABLED" = 1 ]; fi\n')
    control.chmod(0o700)
    library.write_bytes(b'inert network shim fixture')
    substitutions = {
        '/opt/usr/cc2-network-manager/control': shlex.quote(str(control)),
        '/opt/usr/cc2-network-manager/libcc2-network-compat-r1.so': shlex.quote(str(library)),
        receipt['manager_sha256']: hashlib.sha256(control.read_bytes()).hexdigest(),
        receipt['modules']['libcc2-network-compat-r1.so']: hashlib.sha256(library.read_bytes()).hexdigest(),
    }
    for before, after in substitutions.items():
        if helper.count(before) != 1:
            raise RuntimeError('Unexpected network preload test anchor: ' + before)
        helper = helper.replace(before, after)
    for enabled, intact, expected, commands in (
            ('0', True, '0', ['--enabled']),
            ('1', True, '1', ['--enabled', '--ensure']),
            ('1', False, '0', [])):
        log.write_text('')
        if not intact:
            library.write_bytes(b'changed shim')
        env = {**os.environ, 'CC2_TEST_LOG': str(log), 'CC2_TEST_ENABLED': enabled}
        result = subprocess.run(['sh', '-e'], input=helper + '\nprintf "%s\\n" "$network_enabled"\n',
                                text=True, capture_output=True, env=env, check=True, timeout=10)
        if result.stdout.strip() != expected or log.read_text().splitlines() != commands:
            raise RuntimeError('Unexpected combined network preload decision')
    print('PASS: Connections preload enabled, disabled and altered shim fixtures')


def check_rfid_retirement(root, scratch, rfid, component):
    """Run the boot migrator on private, disposable directories before signing."""
    if os.geteuid() != 0:
        return
    work = scratch / 'retirement-test'
    work.mkdir()
    target = work / 'cc2-rfid'
    current = component / 'app-integration.json'
    binary = scratch / 'retire-test'
    subprocess.run(['cc', '-O2', '-std=c11', '-Wall', '-Wextra', '-Wpedantic', '-Werror',
                    '-I' + str(rfid / 'host'), f'-DRETIRE_PARENT="{work}"',
                    f'-DRETIRE_LOCK="{scratch / "retire-test.lock"}"',
                    f'-DRETIRE_CURRENT="{current}"', rfid / 'lifecycle/retire_stock13.c',
                    '-o', binary], check=True, timeout=120)
    backup = next((root / 'opt/inst/ota').glob('*00.00.00.44-app.bin')).read_bytes()
    profile = (component / 'recovery-profile').read_bytes()
    for case in ('absent', 'current', 'dormant', 'restored', 'active', 'pending', 'orphan',
                 'corrupt', 'altered_component', 'unexpected_payload'):
        if case != 'absent':
            shutil.copytree(component, target)
            (target / 'payload').mkdir(mode=0o700)
        if case not in ('absent', 'current'):
            metadata = json.loads(current.read_bytes())
            metadata['app_sha256'] = '1' * 64
            (target / current.name).write_text(json.dumps(metadata, sort_keys=True) + '\n')
        if case in ('restored', 'active', 'corrupt'):
            record = bytearray(128)
            record[:14] = b'CC2-CANVAS-LC1'
            record[16] = 5 if case == 'active' else 10
            record[24] = 1
            record[32] = 1
            record[48:84] = b'12345678-1234-1234-1234-123456789abc'
            record[84:116] = hashlib.sha256(profile).digest()
            journal = target / 'lifecycle'
            journal.write_bytes(record + (b'0' * 64 if case == 'corrupt' else
                                          hashlib.sha256(record).hexdigest().encode()))
            journal.chmod(0o600)
            image = target / 'original-stock-44.bin'
            image.write_bytes(backup)
            image.chmod(0o600)
        if case in ('restored', 'orphan'):
            marker = target / 'enabled'
            marker.write_bytes(b'CC2-RFID-NATIVE-R2\n')
            marker.chmod(0o600)
        if case == 'pending':
            (target / '.lifecycle-123-abcd.next').touch()
        if case == 'altered_component':
            (target / 'coordinator').write_bytes(b'changed')
        if case == 'unexpected_payload':
            (target / 'payload' / 'foreign').touch()
        result = subprocess.run([binary], check=True, capture_output=True, timeout=10)
        archives = list(work.glob('cc2-rfid-before-update.*'))
        expected = case in ('dormant', 'restored')
        if bool(archives) != expected or target.exists() != (case != 'absent' and not expected):
            raise RuntimeError(f'Unexpected Canvas migration decision for {case}: {result.stdout!r}')
        if expected:
            expected_state = b'state=restored' if case == 'restored' else b'state=dormant'
            if expected_state not in result.stdout:
                raise RuntimeError(f'Canvas migration did not report the state for {case}')
            saved = archives[0] / 'component'
            if not (saved / current.name).is_file() or (case == 'restored' and
                    (saved / 'original-stock-44.bin').read_bytes() != backup):
                raise RuntimeError('Canvas migration lost the old component or original backup')
            shutil.rmtree(archives[0])
        elif target.exists():
            shutil.rmtree(target)
    print('PASS: .13 Canvas migration preserves current, active and unsafe states and archives dormant/restored')

    project = PORT.parents[1]
    legacy_archives = [
        project / 'work/cc2-r6-reconstruction/inputs/1.2.26-exact-binding-runtime.tar.gz',
        project / 'outputs/CC2-Control-1.2.27-Release-Final/CC2-Control-1.2.27-Native-Runtime.tar.gz',
        project / ('outputs/CC2-Control-1.2.28-Private-20261109-235017-601/'
                   'CC2-Control-1.2.28-Private-Native-Runtime.tar.gz'),
    ]
    for archive in legacy_archives:
        if not archive.is_file():
            print(f'SKIP: local Canvas migration fixture unavailable: {archive.name}')
            continue
        with tarfile.open(archive) as runtime:
            nested = next((m for m in runtime if m.isfile() and Path(m.name).name == 'rfid.tar.gz'), None)
            component_archive = (tarfile.open(fileobj=io.BytesIO(runtime.extractfile(nested).read()))
                                 if nested else nullcontext(runtime))
            with component_archive as bundle:
                files = {
                    m.name.removeprefix('component/'): (bundle.extractfile(m).read(), m.mode)
                    for m in bundle.getmembers() if m.isfile() and m.name.startswith('component/')
                }
        for case in ('exact', 'modified', 'pending'):
            target.mkdir(mode=0o700)
            (target / 'payload').mkdir(mode=0o700)
            for name, (data, mode) in files.items():
                relative = Path(name)
                if relative.is_absolute() or '..' in relative.parts or mode not in (0o600, 0o700):
                    raise RuntimeError(f'Unsafe Canvas fixture entry: {name}')
                path = target / relative
                path.write_bytes(data)
                path.chmod(mode)
            if case == 'modified':
                (target / 'payload/0').write_bytes(b'changed')
            if case == 'pending':
                (target / 'activation-request').touch()
            result = subprocess.run([binary], check=True, capture_output=True, timeout=10)
            archives = list(work.glob('cc2-rfid-before-update.*'))
            if case == 'exact':
                if target.exists() or len(archives) != 1 or b'state=dormant' not in result.stdout:
                    raise RuntimeError(f'Canvas migration refused exact fixture: {archive.name}')
                saved = archives[0] / 'component'
                if any((saved / name).read_bytes() != data for name, (data, _) in files.items()):
                    raise RuntimeError(f'Canvas migration altered archived files: {archive.name}')
                shutil.rmtree(archives[0])
            else:
                if archives or not target.is_dir():
                    raise RuntimeError(f'Canvas migration moved {case} fixture: {archive.name}')
                shutil.rmtree(target)
        print(f'PASS: exact dormant Canvas archived and altered/pending refused: {archive.name}')


def stage_dormant_panel(root, scratch, rfid, prefix, profile, pins, rows,
                        migration_from=None, preserve_binding=None, active_from=None):
    """Assemble the exact dormant panel runtime in the offline RootFS."""
    sys.path.insert(0, str(rfid / 'lifecycle'))
    from publish_bundle import configuration

    capture = scratch / 'capture'
    coordinator = scratch / 'coordinator'
    capture_pins = {
        'CT_MODULE': rows['guard'][1],
        'CT_MODULE_SHA': rows['guard'][0],
        'CT_EXE_SHA': PRINTER_PULLBACK13_SHA256,
        'CT_GUARD': '/opt/usr/cc2-rfid/coordinator',
        'CT_GUARD_SHA': hashlib.sha256(coordinator.read_bytes()).hexdigest(),
        'CT_LOGROOT': '/opt/usr/cc2-rfid',
    }
    flags = ['-O2', '-std=c11', '-Wall', '-Wextra', '-Wpedantic', '-Werror',
             '-static', '-s', '-pthread', '-mcpu=cortex-a7', '-mfpu=neon-vfpv4', '-mfloat-abi=hard']
    subprocess.run([prefix + 'gcc', *flags, '-DCT_GUARD_NATIVE=1',
                    '-I' + str(rfid / 'host'), *(f'-D{k}="{v}"' for k, v in capture_pins.items()),
                    rfid / 'capture.c', '-o', capture], check=True, timeout=120)
    relative = 'opt/inst/cc2-rfid13'
    destination = root / relative
    component = destination / 'component'
    component.mkdir(parents=True)
    component.chmod(0o700)
    files = {name: (scratch / name).read_bytes()
             for name in ('capture', 'coordinator', 'transfer-worker')}
    files['libcc2-rfid.so'] = (root / 'opt/inst/cc2-printer-extensions13/libcc2-rfid.so').read_bytes()
    files['recovery-profile'] = profile
    files['app-integration.json'] = (json.dumps({
        'protocol': 1,
        'app_sha256': pins['SC_APP_SHA'],
        'coordinator_sha256': capture_pins['CT_GUARD_SHA'],
        'capture_sha256': hashlib.sha256(files['capture']).hexdigest(),
        'profile_sha256': pins['SC_PROFILE_SHA'],
    }, sort_keys=True) + '\n').encode()
    if migration_from:
        new_digest = hashlib.sha256(files['app-integration.json']).hexdigest()
        if new_digest == migration_from:
            raise RuntimeError('Canvas migration trial would reuse the old RFID metadata')
        print(f'PASS: .13 Canvas migration trial changes RFID metadata {migration_from} -> {new_digest}')
    if preserve_binding:
        binding_digest = hashlib.sha256(files['app-integration.json']).hexdigest()
        if binding_digest != preserve_binding:
            raise RuntimeError(f'Active Canvas app binding changed: expected '
                               f'{preserve_binding}, got {binding_digest}')
        print(f'PASS: .13 active Canvas app binding preserved {binding_digest}')
    modes = {name: 0o700 if name in ('capture', 'coordinator', 'transfer-worker') else 0o600 for name in files}
    for name, data in files.items():
        (component / name).write_bytes(data)
        (component / name).chmod(modes[name])
    deps = [(path, digest, (root / path.lstrip('/')).stat().st_mode & 0o777)
            for name, (digest, path) in rows.items() if name in ('bootstrap', 'entry', 'recover', 'marker')]
    app_path = pins['SC_APP_PATH']
    reqhash(root / app_path.lstrip('/'), pins['SC_APP_SHA'], '.13 resident app for Canvas')
    deps.append((app_path, pins['SC_APP_SHA'], 0o755))
    header = scratch / 'publisher-config.h'
    header.write_bytes(configuration(files, modes, deps))
    publisher = destination / 'publisher'
    subprocess.run([prefix + 'gcc', *flags, f'-DCP_CONFIG="{header}"',
                    rfid / 'lifecycle/publish_worker.c', '-o', publisher],
                   check=True, timeout=120)
    publisher.chmod(0o700)
    retire = destination / 'retire-inactive'
    subprocess.run([prefix + 'gcc', *flags, '-I' + str(rfid / 'host'),
                    rfid / 'lifecycle/retire_stock13.c',
                    '-o', retire], check=True, timeout=120)
    retire.chmod(0o700)
    check_rfid_retirement(root, scratch, rfid, component)
    bootstrap = destination / 'seed.sh'
    bootstrap.write_text(f"""#!/bin/sh
set -eu
target=/opt/usr/cc2-rfid
lock=/tmp/cc2-control-install.lock
if [ -e "$target" ] || [ -L "$target" ]; then exit 0; fi
umask 077
mkdir "$lock" || exit 1
trap 'rmdir "$lock"' 0
trap 'exit 1' 1 2 15
/{relative}/publisher --publish /{relative}/component
""", encoding='utf-8', newline='\n')
    bootstrap.chmod(0o700)
    rebind = ''
    if active_from:
        binding, old_app, old_coordinator, old_capture, old_transfer, old_guard = active_from
        same_app=old_app==pins['SC_APP_SHA']
        subprocess.run([sys.executable, '-B', rfid / 'lifecycle/test_app_binding_tx.py'],
                       env={**os.environ, 'CC2_TEST_IMMUTABLE_BINDING': '1',
                            'CC2_TEST_SAME_APP_HELPERS': '1' if same_app else '0',
                            'CC2_TEST_PRESERVE_LIFECYCLE': '0'}, check=True, timeout=180)
        old_metadata = dict(protocol=1, app_sha256=old_app, coordinator_sha256=old_coordinator,
                            capture_sha256=old_capture, profile_sha256=pins['SC_PROFILE_SHA'])
        if hashlib.sha256((json.dumps(old_metadata, sort_keys=True) + '\n').encode()).hexdigest() != binding:
            raise RuntimeError('Active predecessor hashes do not reconstruct its exact RFID metadata')
        if (old_transfer != pins['SC_TRANSFER_SHA']
                or old_guard != rows['guard'][0]):
            raise RuntimeError('Active update requires unchanged transfer worker and loaded guard')
        entries = []
        for name, before in (('coordinator', old_coordinator), ('capture', old_capture),
                             ('app-integration.json', binding)):
            after = hashlib.sha256(files[name]).hexdigest()
            if before == after:
                raise RuntimeError('Expected changed app binding file: ' + name)
            entries.append(f'{{"{name}","{before}","{after}",0{modes[name]:o}}}')
        plan = scratch / 'immutable-binding-plan.h'
        plan.write_text('static const struct au_plan au_boot_plan = {'
                        f'"{old_app}","{pins["SC_APP_SHA"]}","{pins["SC_PROFILE_SHA"]}",'
                        '{' + ','.join(entries) + '}};\n', encoding='ascii')
        worker = destination / 'rebind-active'
        subprocess.run([prefix + 'gcc', *flags, '-I' + str(rfid / 'host'),
                        *(['-DAU_SAME_APP_HELPER_UPDATE=1'] if same_app else []),
                        f'-DAU_BOOT_PLAN="{plan}"', f'-DAU_TRANSFER_SHA="{old_transfer}"',
                        f'-DAU_APP="{app_path}"',
                        f'-DAU_DIRECTORY="/opt/usr/cc2-rfid-immutable-{binding}-{hashlib.sha256(files["app-integration.json"]).hexdigest()}"',
                        rfid / 'lifecycle/immutable_app_binding.c', '-o', worker],
                       check=True, timeout=120)
        worker.chmod(0o700)
        rebind = f'/{relative}/rebind-active || exit 1\n'
        print('PASS: .13 active Canvas rebind worker compiled for three app-bound files')
    launch = root / 'opt/inst/cc2-control/start.sh'
    data = launch.read_bytes()
    anchor = b'exec "$BASE/cc2-control" '
    if data.count(anchor) != 1:
        raise RuntimeError('Unexpected resident app startup anchor')
    data = data.replace(anchor,
                        (f'sh /{relative}/seed.sh || logger -t cc2-control "Canvas component seed deferred"\n'
                         + rebind + '\n').encode()
                        + anchor)
    launch.write_bytes(data)
    for script in (bootstrap, launch):
        subprocess.run(['sh', '-n', script], check=True, timeout=10)
    for name in ('lifecycle', 'lifecycle.next', 'enabled', 'activation-request'):
        if os.path.lexists(component / name):
            raise RuntimeError('Dormant RFID component contains active state')
    print('PASS: .13 dormant panel component, capture and publisher staged; no activation state')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--inputs-only', action='store_true', help='Check input locations and the stock image hash')
    parser.add_argument('--with-app', action='store_true', help='Build and include the current local CC2 Control app')
    parser.add_argument('--with-recovery', action='store_true', help='Compile and test the Canvas recovery launcher')
    parser.add_argument('--with-transfer', action='store_true', help='Compile and bind the Canvas transfer worker without executing it')
    parser.add_argument('--package', type=Path, help='Write a verified private Community .zip.sig')
    parser.add_argument('--signing-mode', choices=('stock', 'community'), default='stock',
                        help='Stock (default) for ELEGOO and Dual Trust; Community for Dual Trust only')
    parser.add_argument('--private-key', type=Path, help='Local key for the selected signing mode')
    parser.add_argument('--aes-key', type=Path, help='Local pinned AES key, used only for --package')
    parser.add_argument('--mksquashfs', type=Path)
    parser.add_argument('--unsquashfs', type=Path)
    parser.add_argument('--toolchain-manifest', type=Path)
    parser.add_argument('--rfid-migration-from', help='Require a change from this app-integration.json SHA-256')
    parser.add_argument('--rfid-preserve-profile', help='Require this active Canvas recovery-profile SHA-256')
    parser.add_argument('--rfid-preserve-binding', help='Require this active Canvas app-integration.json SHA-256')
    parser.add_argument('--rfid-active-from', nargs=6, metavar='SHA256',
                        help='Exact old binding, app, coordinator, capture, transfer worker and guard hashes')
    args = parser.parse_args()
    if args.package:
        if args.private_key is None or args.aes_key is None:
            parser.error('--package requires --private-key and --aes-key')
        args.with_app = args.with_recovery = args.with_transfer = True
    if args.with_transfer and not (args.with_app and args.with_recovery):
        parser.error('--with-transfer requires --with-app and --with-recovery')
    for option, digest in (('--rfid-migration-from', args.rfid_migration_from),
                           ('--rfid-preserve-profile', args.rfid_preserve_profile),
                           ('--rfid-preserve-binding', args.rfid_preserve_binding)):
        if digest is not None and (not args.with_transfer or not re.fullmatch('[0-9a-f]{64}', digest)):
            parser.error(f'{option} requires a lowercase SHA-256 and --with-transfer or --package')
    if args.rfid_preserve_binding and (not args.rfid_preserve_profile or args.rfid_migration_from):
        parser.error('--rfid-preserve-binding requires --rfid-preserve-profile and excludes --rfid-migration-from')
    if args.rfid_active_from and (not args.rfid_preserve_profile or args.rfid_preserve_binding or
                                 args.rfid_migration_from or
                                 any(not re.fullmatch('[0-9a-f]{64}', sha) for sha in args.rfid_active_from)):
        parser.error('--rfid-active-from requires the preserved profile and six lowercase SHA-256 values')
    stock = PORT.parent / 'cc2-stock-02.01.01.13/rootfs.squashfs'
    stock07_gui = PORT.parent / 'cc2-stock-02.01.01.07/selected/opt/bin/ec-eeb001-gui'
    archive = PORT.parents[1] / 'outputs' / ARCHIVE
    reactor = PORT / 'reactor13-prepared'
    camera = PORT / 'native13-check/connections-camera-30452-24235'
    extensions = find_extensions(PORT / 'native13-check')
    if extensions is None:
        parser.error('no readable qualified RFID/EXHAUST receipt with matching module hashes')
    print('RFID/EXHAUST input: ' + extensions.name)
    for item in (stock07_gui, archive, reactor / 'prepared-manifest.json',
                 camera / 'native13-check.json'):
        if not item.is_file():
            parser.error('missing input ' + str(item))
    reqhash(stock, ROOTFS_SHA, '.13 original RootFS')
    reqhash(camera / 'libcc2-camera-recovery-r2.so',
            'c3e953758fe3690cc38a3b3f63fa7c6a653001828f3f9285e977e88281ecc1dc',
            '.13 Camera Recovery verified on printer')
    if args.inputs_only:
        return
    if sys.platform != 'linux':
        parser.error('Run this check in WSL/Linux to preserve Linux file modes and links')
    if args.package and os.geteuid() != 0:
        parser.error('--package requires root in WSL to preserve the original device node')
    for tool in ('awk', 'sh', 'bash') + (() if args.package else ('unsquashfs',)):
        if not shutil.which(tool):
            parser.error('missing Linux tool ' + tool)
    with TemporaryDirectory(prefix='cc2-combined13-', dir='/tmp') as scratch:
        if args.package:
            import firmware_builder as fw
            from pack_trial13 import STOCK_SWU_SHA, package_trial
            target = args.package.absolute()
            if os.path.lexists(target) or not target.parent.is_dir() or not target.name.endswith('.zip.sig'):
                parser.error('Choose a new .zip.sig path in an existing private output directory')
            stock_swu = stock.with_name('swu.plain')
            reqhash(stock_swu, STOCK_SWU_SHA, '.13 verified decrypted stock SWU')
            public = fw.verify_signing_key(
                args.private_key, args.signing_mode, dual_payload=fw.PORT_DIR / 'dual_verify_44.bin')
            reqhash(args.aes_key, fw.EXPECTED['aes_key'], 'AES key v1')
            aes = args.aes_key.read_bytes()
            mk = fw.find_squash_tool(args.mksquashfs, 'mksquashfs')
            un = fw.find_squash_tool(args.unsquashfs, 'unsquashfs')
            fw.verify_squashfs(mk, un, Path(scratch), args.toolchain_manifest)
            if not shutil.which('cpio'):
                parser.error('missing Linux tool cpio')
        manager_sha = None
        if args.with_app:
            camera, manager_sha = prepare_test_manager(Path(scratch), camera)
        app = prepare_test_app(Path(scratch), manager_sha) if args.with_app else None
        recovery = None
        if args.with_recovery:
            recovery = Path(scratch) / 'recover-worker'
            prefix = '/opt/cc2-cross/toolchain-out/bin/arm-cortex_a15-linux-gnueabihf-'
            rfid = PORT / 'source/experimental/canvas-rfid'
            subprocess.run([prefix + 'gcc', '-O2', '-std=c11', '-Wall', '-Wextra', '-Wpedantic',
                            '-Werror', '-static', '-mcpu=cortex-a7', '-mfpu=neon-vfpv4',
                            '-mfloat-abi=hard', '-I' + str(rfid / 'host'),
                            '-DCL_NATIVE_SHA="' + PRINTER_PULLBACK13_SHA256 + '"',
                            rfid / 'lifecycle/recover_worker.c', '-o', recovery],
                           check=True, timeout=120)
        root = Path(scratch) / 'rootfs'
        # The scratch audit does not open device nodes. Exclude just the
        # stock console so extraction works without root and other errors fail.
        extraction = [un, '-processors', '2', '-d', root, stock] if args.package else [
            'unsquashfs', '-processors', '2', '-d', root, '-excludes', stock, 'dev/console']
        subprocess.run(extraction, check=True, timeout=180)
        stage(root, app=app, network_trial=True, stock07_gui=stock07_gui,
              reactor_component=reactor, camera_recovery_component=camera,
              printer_extensions=extensions, connections_runtime=archive, recovery_worker=recovery)
        for name in ('etc/init.d/printer', 'opt/bin/run_printer.sh',
                     'etc/init.d/cc2-camera-launch', 'etc/init.d/cc2-reactor-launch',
                     'opt/inst/cc2-printer-extensions13/launch.sh'):
            subprocess.run(['sh', '-n', root / name], check=True, timeout=10)
        launcher = (root / 'opt/inst/cc2-printer-extensions13/launch.sh').read_text()
        start = launcher.index('# CC2_CANVAS_GATE_BEGIN\n')
        end = launcher.index('# CC2_CANVAS_GATE_END\n', start)
        guard = launcher[start:end]
        state = Path(scratch) / 'canvas-fixture'
        state.mkdir()
        for name in (None, 'lifecycle', 'lifecycle.next', 'enabled'):
            evidence = state / name if name else None
            if evidence is not None:
                evidence.touch()
            script = '\n'.join(
                f'{key}={shlex.quote(str(state / name))}'
                for key, name in (('journal', 'lifecycle'), ('pending', 'lifecycle.next'),
                                  ('marker', 'enabled')))
            result = subprocess.run(['sh', '-e'], input=script + '\nstop() { exit 125; }\nmatches() { return 1; }\nsource=/missing\n' + guard,
                                    text=True, timeout=10)
            if evidence is not None:
                evidence.unlink()
            if result.returncode != (0 if evidence is None else 125):
                raise RuntimeError('Unexpected Canvas boot interlock decision')
        print('PASS: dormant Canvas allowed; unresolved journal, pending write and orphan marker refused')
        if recovery is not None:
            profile = canvas_recovery_profile13(root, extensions, PRINTER_PULLBACK13_SHA256)
            if args.rfid_preserve_profile:
                actual_profile = hashlib.sha256(profile).hexdigest()
                if actual_profile != args.rfid_preserve_profile:
                    raise RuntimeError(f'Active Canvas recovery profile changed: expected '
                                       f'{args.rfid_preserve_profile}, got {actual_profile}')
                print(f'PASS: .13 active Canvas recovery profile preserved {actual_profile}')
            rows = dict((name, (digest, path)) for name, digest, path in
                        (row.split() for row in profile.decode().splitlines()))
            for name in ('bootstrap', 'entry', 'recover', 'marker'):
                digest, path = rows[name]
                reqhash(root / path.lstrip('/'), digest, '.13 Canvas profile ' + name)
            if rows['guard'][1] != '/opt/usr/cc2-rfid/libcc2-rfid.so':
                raise RuntimeError('Canvas guard path differs from the panel runtime')
            print('PASS: .13 Canvas recovery profile binds the final launchers and panel guard path')
            if app is not None:
                _, manifest = app
                pins = {
                    'SC_APP_PATH': '/opt/inst/cc2-control/cc2-control',
                    'SC_PROFILE_SHA': hashlib.sha256(profile).hexdigest(),
                    'SC_APP_SHA': manifest['files']['cc2-control']['sha256'],
                    'SC_NATIVE_SHA': PRINTER_PULLBACK13_SHA256,
                    'SC_RUN_SHA': hashlib.sha256((root / 'opt/bin/run_printer.sh').read_bytes()).hexdigest(),
                    'SC_REACTOR_SHA': hashlib.sha256((root / 'opt/inst/cc2-reactor/libcc2-reactor-v2.so').read_bytes()).hexdigest(),
                    'SC_CAVITY_SHA': hashlib.sha256((extensions / 'libcc2-cavity-floor-r1.so').read_bytes()).hexdigest(),
                    'SC_NETWORK_SHA': hashlib.sha256((camera / 'libcc2-network-compat-r1.so').read_bytes()).hexdigest(),
                    'SC_STRICT_SHA': rows['guard'][0],
                }
                if args.with_transfer:
                    transfer = Path(scratch) / 'transfer-worker'
                    transfer_pins = {
                        'CV_BASE': '/opt/usr/cc2-rfid',
                        'CV_MODULE': rows['guard'][1],
                        'CV_MODULE_SHA': rows['guard'][0],
                        'CV_EXE_SHA': PRINTER_PULLBACK13_SHA256,
                    }
                    subprocess.run([prefix + 'gcc', '-O2', '-std=c11', '-Wall', '-Wextra',
                                    '-Wpedantic', '-Werror', '-static', '-s', '-pthread',
                                    '-mcpu=cortex-a7', '-mfpu=neon-vfpv4', '-mfloat-abi=hard',
                                    '-DCV_COORDINATED=1', '-DCV_NATIVE_RESTORE_GUARD=1',
                                    *(f'-D{name}="{value}"' for name, value in transfer_pins.items()),
                                    rfid / 'transfer/canvas_worker.c', '-o', transfer, '-lm'],
                                   check=True, timeout=120)
                    pins['SC_TRANSFER_SHA'] = hashlib.sha256(transfer.read_bytes()).hexdigest()
                    print('PASS: .13 Canvas transfer worker compiled and pinned')
                subprocess.run([prefix + 'gcc', '-O2', '-std=c11', '-Wall', '-Wextra', '-Wpedantic',
                                '-Werror', '-static', '-s', '-pthread', '-mcpu=cortex-a7', '-mfpu=neon-vfpv4',
                                '-mfloat-abi=hard', '-DSC_IMMUTABLE_BOOT=1', '-DSC_STOCK_BOOTSTRAP=1',
                                '-DSC_NATIVE_RESTORE_GUARD=1', '-I' + str(rfid / 'host'),
                                *(f'-D{name}="{value}"' for name, value in pins.items()),
                                rfid / 'lifecycle/coordinator.c', '-o', Path(scratch) / 'coordinator', '-lm'],
                               check=True, timeout=120)
                transfer_status = 'worker bound, not executed' if args.with_transfer else 'transfer disabled'
                print(f'PASS: .13 immutable Canvas activation coordinator compiled for ARM ({transfer_status})')
                if args.with_transfer:
                    stage_dormant_panel(root, Path(scratch), rfid, prefix, profile, pins, rows,
                                        args.rfid_migration_from, args.rfid_preserve_binding, args.rfid_active_from)
            fake = state / 'recover-worker'
            fake.write_text('''#!/bin/sh
case "$1" in
 --recover-original) [ "$TEST_RECOVER" = 1 ] || exit 1; : > "$TEST_LOG";;
 --boot-policy) [ ! -e "$TEST_LOG" ] || exit "$TEST_AFTER"; exit "$TEST_STATUS";;
 *) exit 1;;
esac
''')
            fake.chmod(0o700)
            (state / 'lifecycle').touch()
            marker = state / 'enabled'
            marker.write_text('CC2-RFID-NATIVE-R2\n')
            log = state / 'recovered'
            for status, allow, after, expected, recovered in (
                    (0, 0, 0, 'base', False), (10, 0, 0, 'guarded', False),
                    (20, 1, 10, 'guarded', True), (20, 0, 10, None, False),
                    (20, 1, 0, None, True), (1, 0, 0, None, False)):
                log.unlink(missing_ok=True)
                env = {**os.environ, 'TEST_LOG': str(log), 'TEST_RECOVER': str(allow),
                       'TEST_AFTER': str(after), 'TEST_STATUS': str(status)}
                prelude = script + '\nsource=' + shlex.quote(str(state))
                prelude += '\npreloads=base\nstop() { exit 125; }\nmatches() { return 0; }\n'
                result = subprocess.run(['sh', '-e'], input=prelude + guard + 'printf "%s\\n" "$preloads"\n',
                                        text=True, capture_output=True, timeout=10, env=env)
                output = result.stdout.strip()
                if (result.returncode != (125 if expected is None else 0)
                        or (expected == 'base' and output != 'base')
                        or (expected == 'guarded' and output != 'base:/opt/usr/cc2-rfid/libcc2-rfid.so')
                        or log.exists() != recovered):
                    raise RuntimeError('Unexpected Canvas recovery dispatcher decision')
            print('PASS: Canvas boot recovery dispatcher simulated stock, guarded, recovery and failure')
        marker = root / 'opt/usr/cc2-rfid/enabled'
        if os.path.lexists(marker):
            raise RuntimeError('RFID must remain dormant in the assembled RootFS')
        config = (root / 'opt/inst/printer_dsp.cfg').read_bytes()
        if b'unload_fila_dist=85' not in config:
            raise RuntimeError('Original Canvas pullback default missing')
        receipt = json.loads((camera / 'native13-check.json').read_bytes())
        check_network_preload(root, receipt, Path(scratch))
        print('PASS: .13 combined Linux staging, native launch syntax and original pullback default')
        if args.package:
            ssh = PORT / 'ssh-stage'
            inputs = []
            for name, (relative, pin, _) in fw.SSH_FILES.items():
                item = ssh / relative
                reqhash(item, fw.EXPECTED[pin], '.13 package SSH ' + name)
                inputs.append(item)
            fw.install_ssh(root, *inputs)
            fw.audit_ssh(root)
            package_trial(root, stock_swu, Path(scratch), target,
                          args.private_key, aes, public, mk, un, signing_mode=args.signing_mode)


if __name__ == '__main__':
    main()
