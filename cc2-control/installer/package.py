#!/usr/bin/env python3
"""Package the current ARM build with the tracked updater and rollback scripts."""
import argparse
import gzip
import hashlib
import io
import json
from pathlib import Path
import struct
import tarfile
import zipfile
import sys

sys.path.insert(0, str(Path(__file__).resolve().parent))
from camera_component import functions as camera_functions, RESTORE as CAMERA_RESTORE
from network_component import functions as network_functions, RUNTIME_SHA as NETWORK_RUNTIME_SHA
from app_binding_component import functions as app_binding_functions
from native_delivery import functions as native_functions
from rfid_component import functions as rfid_functions, ORDINARY_GUARD as RFID_ORDINARY_GUARD, RESTORE_GUARD as RFID_RESTORE_GUARD

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[1]
VERSION = (HERE.parent / 'VERSION').read_text().strip()
FIRMWARE_VERSION = (HERE.parent / 'FIRMWARE_VERSION').read_text().strip()
COMPATIBILITY = json.loads((HERE / 'compatibility.json').read_text())
if hashlib.sha256((HERE.parent / 'FIRMWARE_VERSION').read_bytes()).hexdigest() != COMPATIBILITY['firmware_version_file_sha256']:
    raise ValueError('Updater firmware compatibility record does not match FIRMWARE_VERSION')

def package(prepared, scripts, source, output, camera_runtime=None, connections_runtime=None, rfid_runtime=None, app_binding_runtime=None, native_runtime=None):
    if native_runtime and (rfid_runtime or app_binding_runtime):
        raise ValueError("Unified delivery replaces separate RFID/binding inputs")
    if app_binding_runtime and (rfid_runtime or camera_runtime or connections_runtime):
        raise ValueError("App binding trial must preserve installed native components")
    if rfid_runtime and (camera_runtime or connections_runtime):
        raise ValueError('Closed-profile RFID trial must preserve the exact installed camera/network runtime')
    binary = (prepared / 'cc2-control').read_bytes()
    if len(binary) < 20 or binary[:6] != b'\x7fELF\x01\x01' or struct.unpack_from('<H', binary, 18)[0] != 40:
        raise ValueError('Payload requires a 32-bit little-endian ARM ELF')
    binary_hash = hashlib.sha256(binary).hexdigest()
    rfid_code, rfid_tail = rfid_functions(rfid_runtime, binary_hash) if rfid_runtime else ('', '')
    binding_code, binding_tail, binding_restore = app_binding_functions(app_binding_runtime, binary_hash) if app_binding_runtime else ('', '', '')
    native = native_functions(native_runtime, binary_hash) if native_runtime else None
    files = {'cc2-control': binary}
    worker = (prepared / 'cc2-update-worker').read_bytes()
    if len(worker) < 20 or worker[:6] != b'\x7fELF\x01\x01' or struct.unpack_from('<H', worker, 18)[0] != 40:
        raise ValueError('Update verifier requires a 32-bit little-endian ARM ELF')
    files['cc2-update-worker'] = worker
    downloader = (prepared / 'cc2-https-download').read_bytes()
    if len(downloader) < 20 or downloader[:6] != b'\x7fELF\x01\x01' or struct.unpack_from('<H', downloader, 18)[0] != 40:
        raise ValueError('HTTPS helper requires a 32-bit ARM executable')
    files['cc2-https-download'] = downloader
    files['ca-bundle.pem'] = (prepared / 'ca-bundle.pem').read_bytes()
    for name in ('update-runner.sh', 'updater-trusted-key.json', 'THIRD_PARTY_NOTICES.txt'):
        files[name] = (prepared / name).read_bytes()
    for name in ('start.sh', 'launch.sh', 'cc2-control.init'):
        data = (scripts / name).read_bytes().decode('utf-8-sig').replace('\r\n', '\n').replace('\r', '\n').encode()
        if not data.startswith(b'#!'): raise ValueError('Missing shebang: ' + name)
        files[name] = data
    if b'/opt/usr/cc2-control/launch.sh' not in files['cc2-control.init']:
        raise ValueError('Updater init must launch the persistent installation')
    for path in sorted((prepared / 'web').rglob('*')):
        if path.is_file(): files[path.relative_to(prepared).as_posix()] = path.read_bytes()
    if 'web/index.html' not in files or 'web/locales/en.json' not in files:
        raise ValueError('Prepared UI is incomplete')
    for name in ('install-on-printer.sh', 'restore-on-printer.sh'):
        text = (HERE / name).read_text().replace('@BINARY_SHA256@', binary_hash).replace('@VERSION@', VERSION).replace('@FIRMWARE_VERSION@', FIRMWARE_VERSION).replace('@PRINTER_SHA256@', COMPATIBILITY['printer_sha256']).replace('@DAEMON_SHA256@', COMPATIBILITY['daemon_sha256'])
        text = text.replace('@CAMERA_RESTORE_FUNCTION@', CAMERA_RESTORE)
        text = text.replace('@CAMERA_COMPONENT_FUNCTIONS@', camera_functions(HERE / 'camera', camera_runtime) if camera_runtime else '')
        text = text.replace('@CAMERA_COMPONENT_PREFLIGHT@', 'camera_unpack\nsh "$CAMERA_SOURCE/manage.sh" check' if camera_runtime else '')
        text = text.replace('@CONNECTIONS_COMPONENT_FUNCTIONS@', network_functions(connections_runtime) if connections_runtime else '')
        text = text.replace('@CONNECTIONS_COMPONENT_INSTALL@', 'install_phase network-component\n                network_install_component || exit 1' if connections_runtime else '')
        text = text.replace('@RFID_COMPONENT_FUNCTIONS@', rfid_code)
        text = text.replace('@RFID_COMPONENT_PREFLIGHT@', 'rfid_unpack || exit 1' if rfid_runtime else ('binding_unpack || exit 1' if app_binding_runtime else RFID_ORDINARY_GUARD))
        text = text.replace('@RFID_COMPONENT_PUBLISH@', 'rfid_publish || exit 1' if rfid_runtime else '')
        text = text.replace('@RFID_COMPONENT_COMPLETION@', "echo 'RFID ACTIVATION PENDING: after installer unlock, open Canvas RFID and follow full idle-printer reboot prompts. Keep power connected during transfer and preserve all original backups.'" if rfid_runtime else '')
        text = text.replace('@APP_BINDING_FUNCTIONS@', binding_code)
        text = text.replace('@APP_BINDING_PREPARE@', 'binding_prepare || exit 1' if app_binding_runtime else '')
        text = text.replace('@APP_BINDING_COMMIT@', 'binding_commit || exit 1' if app_binding_runtime else '')
        text = text.replace('@APP_BINDING_ROLLBACK@', '''if [ "${BINDING_PREPARED:-0}" -eq 1 ] && ! binding_rollback; then
                echo "RFID app binding rollback incomplete; retain current app, lock and backups. Do not start the old app." >&2
                exit 1
            fi''' if app_binding_runtime else '')
        text = text.replace('@APP_BINDING_RESTORE_FUNCTIONS@', binding_restore)
        text = text.replace('@APP_BINDING_RESTORE_ROLLBACK@', 'exec 3< "$LOCK"\nbinding_rollback || exit 1' if app_binding_runtime else '')
        text = text.replace('@RFID_RESTORE_GUARD@', 'binding_verify_worker || exit 1' if app_binding_runtime else RFID_RESTORE_GUARD)
        if native:
            # Templates have already expanded the ordinary/private hooks; use
            # the same tokens on a fresh template so only one route is emitted.
            text = (HERE / name).read_text().replace('@BINARY_SHA256@', binary_hash).replace('@VERSION@', VERSION).replace('@FIRMWARE_VERSION@', FIRMWARE_VERSION).replace('@PRINTER_SHA256@', COMPATIBILITY['printer_sha256']).replace('@DAEMON_SHA256@', COMPATIBILITY['daemon_sha256'])
            substitutions = {
                '@CAMERA_RESTORE_FUNCTION@': CAMERA_RESTORE,
                '@CAMERA_FAILURE_RELEASE@': "echo 'Native installer barrier retained until camera startup recovery is resolved.' >&2",
                '@CAMERA_COMPONENT_FUNCTIONS@': camera_functions(HERE / 'camera', camera_runtime) if camera_runtime else '',
                '@CAMERA_COMPONENT_PREFLIGHT@': 'camera_unpack\nsh "$CAMERA_SOURCE/manage.sh" check' if camera_runtime else '',
                '@CONNECTIONS_COMPONENT_FUNCTIONS@': network_functions(connections_runtime) if connections_runtime else '',
                '@CONNECTIONS_COMPONENT_INSTALL@': 'install_phase network-component\n                network_install_component || exit 1' if connections_runtime else '',
                '@RFID_COMPONENT_FUNCTIONS@': native['functions'],
                '@RFID_COMPONENT_PREFLIGHT@': native['preflight'],
                '@RFID_COMPONENT_PUBLISH@': native['prepare'],
                '@RFID_COMPONENT_COMPLETION@': "echo 'Native components ready: follow Canvas RFID activation and full idle-printer reboot prompts when requested.'",
                '@RFID_RESTORE_GUARD@': native['restore_guard'],
                '@APP_BINDING_FUNCTIONS@': '', '@APP_BINDING_PREPARE@': '',
                '@APP_BINDING_COMMIT@': native['commit'],
                '@APP_BINDING_ROLLBACK@': native['rollback'],
                '@APP_BINDING_RESTORE_FUNCTIONS@': native['restore_functions'],
                '@APP_BINDING_RESTORE_ROLLBACK@': native['restore_rollback'],
                '@NATIVE_UNCHANGED_ROLLBACK@': native['unchanged_rollback'],
            }
            for token,value in substitutions.items(): text = text.replace(token,value)
        else:
            text = text.replace('@NATIVE_UNCHANGED_ROLLBACK@', '').replace('@CAMERA_FAILURE_RELEASE@', 'rmdir "$LOCK"')
        if name == 'install-on-printer.sh': text += native['tail'] if native else rfid_tail + binding_tail
        files[name] = text.encode()
    files['build-info.json'] = (json.dumps({
        'version': VERSION, 'binary_sha256': binary_hash,
        'source_archive_sha256': hashlib.sha256(source.read_bytes()).hexdigest(),
        'change': 'CC2 Control application updater',
        'camera_component': 'reap-r2+recovery-r2' if camera_runtime else None,
        'connections_runtime_sha256': NETWORK_RUNTIME_SHA if connections_runtime else None,
        'requires_full_printer_reboot': bool(camera_runtime or rfid_runtime or native_runtime),
        'rfid_private_closed_profile': bool(rfid_runtime),
        'rfid_private_same_profile_app_binding': bool(app_binding_runtime),
        'unified_native_delivery': bool(native_runtime),
        'rfid_activation_managed_after_installer_unlock': bool(rfid_runtime or native_runtime),
        'required_community_firmware': FIRMWARE_VERSION,
        'compatibility': COMPATIBILITY,
    }, indent=2) + '\n').encode()
    files['SHA256SUMS'] = ''.join(hashlib.sha256(data).hexdigest() + '  ' + name + '\n'
        for name, data in sorted(files.items())).encode()
    payload = io.BytesIO()
    with gzip.GzipFile(fileobj=payload, mode='wb', mtime=0) as compressed:
        with tarfile.open(fileobj=compressed, mode='w') as archive:
            for name, data in sorted(files.items()):
                entry = tarfile.TarInfo(name); entry.size = len(data)
                entry.mode = 0o755 if name in ('cc2-control', 'cc2-update-worker', 'cc2-https-download') or name.endswith(('.sh', '.init')) else 0o644
                entry.uid = entry.gid = 0; entry.mtime = 0
                archive.addfile(entry, io.BytesIO(data))
    public = {name: (HERE / name).read_bytes() for name in
              ('Install-CC2-Control.ps1', 'Install-Windows.cmd', 'install-cc2-control.sh', 'README.md')}
    for name in ('README-TEST.md', 'README-EXPERIMENTAL.md'):
        trial_notes = HERE / name
        if trial_notes.is_file(): public[name] = trial_notes.read_bytes()
    public['cc2-control-payload.tar.gz'] = payload.getvalue()
    public['pc-stage.sh'] = (HERE / 'pc-stage.sh.in').read_text(encoding='utf-8').replace('@PAYLOAD_SHA@', hashlib.sha256(payload.getvalue()).hexdigest()).encode('utf-8')
    public['cc2-control-sources.zip'] = source.read_bytes()
    public['SHA256SUMS'] = ''.join(hashlib.sha256(data).hexdigest() + '  ' + name + '\n'
        for name, data in sorted(public.items())).encode()
    output.parent.mkdir(parents=True, exist_ok=True)
    with zipfile.ZipFile(output, 'w', zipfile.ZIP_DEFLATED) as archive:
        for name, data in sorted(public.items()):
            entry = zipfile.ZipInfo(output.stem + '/' + name, (2026, 1, 1, 0, 0, 0))
            entry.compress_type = zipfile.ZIP_DEFLATED
            entry.external_attr = (0o100755 if name.endswith(('.sh', '.cmd')) else 0o100644) << 16
            archive.writestr(entry, data)
    print('Updater:', output)
    print('ARM binary SHA256:', binary_hash)
    output.with_suffix(output.suffix + '.sha256').write_text(hashlib.sha256(output.read_bytes()).hexdigest() + '  ' + output.name + '\n')
    print('Updater SHA256:', hashlib.sha256(output.read_bytes()).hexdigest())

if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--prepared', type=Path, default=ROOT / 'builder/current/components/cc2-control/prepared')
    parser.add_argument('--scripts', type=Path, default=HERE.parent / 'scripts')
    parser.add_argument('--source', type=Path, default=ROOT / 'builder/current/components/cc2-control/source/source.zip')
    parser.add_argument('--output', type=Path, default=ROOT / f'output/CC2-Control-{VERSION}-Multiplatform-Update.zip')
    parser.add_argument('--camera-runtime', type=Path, help='Include the three fingerprinted R2 camera runtime files')
    parser.add_argument('--release', action='store_true', help='Require the cumulative camera component for a public release')
    parser.add_argument('--connections-runtime', type=Path, help='Include the exact qualified optional Connections runtime')
    parser.add_argument('--application-only-trial', action='store_true', help='Explicitly build a development-only app package without the cumulative camera component')
    parser.add_argument('--rfid-runtime', type=Path, help='Private closed-profile dormant RFID component; not a public release')
    parser.add_argument('--app-binding-runtime', type=Path, help='Private same-profile app upgrade; not public native delivery')
    parser.add_argument('--native-runtime', type=Path, help='Unified first-install and active native update runtime; qualification build only')
    args = parser.parse_args()
    if args.release and args.application_only_trial:
        parser.error('--release and --application-only-trial are mutually exclusive')
    if args.native_runtime and (args.release or not args.application_only_trial):
        parser.error('Unified native delivery requires --application-only-trial until complete qualification')
    if args.app_binding_runtime and (args.release or not args.application_only_trial):
        parser.error('App binding runtime requires --application-only-trial; public delivery remains disabled')
    if args.rfid_runtime and (args.release or not args.application_only_trial):
        parser.error('RFID runtime currently requires --application-only-trial and cannot be published')
    if args.camera_runtime is None and (args.release or not args.application_only_trial):
        parser.error('Public cumulative releases require --camera-runtime, including for users who skipped earlier updates')
    if args.release and args.connections_runtime is None:
        parser.error('Public cumulative releases require --connections-runtime; unsupported native profiles retain native Wi-Fi')
    package(args.prepared, args.scripts, args.source, args.output, args.camera_runtime, args.connections_runtime, args.rfid_runtime, args.app_binding_runtime, args.native_runtime)
