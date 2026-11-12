"""Prepare the exact .13 vendor launch scripts for the native components."""

import hashlib
import json
from pathlib import Path

from camera_component import patched_init
from boot_diagnostics import shell_note
from connections_component import STOCK_13, check_native
from reactor_component import FILES, LIBCO_SHA256, patched_printer_init, patched_vendor_launcher


REACTOR13_MANIFEST_SHA256 = 'cfc8641daa0d1ea3360f0809024ab46a0a1beb1a6f237ae6d13240d677c046e0'
REACTOR_SOURCE = Path(__file__).resolve().parents[3] / 'experimental/firmware-port/reactor'
CAMERA13_SHA256 = '90da7634c21b74b3bf943d68129da78668b47f9706620e158283697ab95553a7'
CAMERA_RECOVERY13 = 'libcc2-camera-recovery-r2.so'
QUALIFIED_MODULES = {'libcc2-network-compat-r1.so', CAMERA_RECOVERY13}
PRINTER13_EXTENSIONS = ('libcc2-cavity-floor-r1.so', 'libcc2-rfid.so')


STOCK_STARTUP = {
    'etc/init.d/printer': STOCK_13['etc/init.d/printer'],
    'opt/bin/run_printer.sh': 'c26d495c6ea4870cb36a42c0e291db59caf2a9cfaa8782a3befeb766106581e0',
}


def prepare(root):
    root = Path(root)
    check_native(root, STOCK_STARTUP)
    init = (root / 'etc/init.d/printer').read_bytes().decode('utf-8')
    launcher = (root / 'opt/bin/run_printer.sh').read_bytes().decode('utf-8')
    reactor_init = patched_printer_init(init)
    return {
        'etc/init.d/printer': patched_init(reactor_init).encode('utf-8'),
        'opt/bin/run_printer.sh': patched_vendor_launcher(launcher).encode('utf-8'),
    }


def verify_qualified_component(component, camera, printer, printer_sha256):
    """Verify the ARM qualification receipt against the intended executable pair."""
    component = Path(component)
    receipt = json.loads((component / 'native13-check.json').read_bytes())
    if (receipt['camera_sha256'] != CAMERA13_SHA256
            or receipt['printer_sha256'] != printer_sha256
            or set(receipt['modules']) != QUALIFIED_MODULES):
        raise RuntimeError('Connections and Camera Recovery receipt does not match the .13 profile')
    check_native(camera.parent, {camera.name: CAMERA13_SHA256})
    check_native(printer.parent, {printer.name: printer_sha256})
    check_native(component, {**receipt['modules'], 'camera-recovery-test': receipt['test_sha256']})
    return receipt['modules']


def stage_camera_recovery(root, component, modules):
    """Stage Camera Recovery at the vendor camera position in a scratch RootFS."""
    relative = 'opt/inst/cc2-camera-recovery13/' + CAMERA_RECOVERY13
    target = root / relative
    hook = root / 'etc/init.d/cc2-camera-launch'
    if any(path.exists() or path.is_symlink() for path in (target.parent, hook)):
        raise RuntimeError('Existing camera installation in scratch RootFS')
    init = root / 'etc/init.d/printer'
    updated = patched_init(init.read_bytes().decode()).encode()
    launcher = f"""#!/bin/sh
set -eu
{shell_note()}
camera=/opt/bin/ai_camera
recovery=/{relative}
export LD_LIBRARY_PATH=/opt/lib
if pidof ai_camera >/dev/null 2>&1; then
    exit 1
fi
if [ -z "${{LD_PRELOAD:-}}" ] &&
    [ -f "$recovery" ] && [ ! -L "$recovery" ] &&
    printf '%s  %s\\n' '{CAMERA13_SHA256}' "$camera" '{modules[CAMERA_RECOVERY13]}' "$recovery" |
        sha256sum -c >/dev/null 2>&1; then
    cc2_boot_note camera 'LAUNCH=verified_recovery'
    CC2_CAMERA_RECOVERY_TRIAL=1 LD_PRELOAD="$recovery" exec "$camera" "$@"
fi
logger -t cc2-camera 'Camera Recovery .13 preflight refused; using native camera' || true
cc2_boot_note camera 'FALLBACK=native_camera recovery_preflight_refused'
CC2_CAMERA_RECOVERY_TRIAL=0 exec "$camera" "$@"
"""
    target.parent.mkdir(parents=True)
    target.write_bytes((component / CAMERA_RECOVERY13).read_bytes())
    target.chmod(0o600)
    check_native(root, {relative: modules[CAMERA_RECOVERY13]})
    hook.write_bytes(launcher.encode())
    hook.chmod(0o755)
    init.write_bytes(updated)


def stage_reactor(root, component, printer_sha256):
    """Stage the reviewed local .13 Reactor build and preserve the stock launch scripts."""
    root, component = Path(root), Path(component)
    check_native(root, {
        **STOCK_STARTUP,
        'opt/bin/elegoo_printer': printer_sha256,
        'opt/lib/libcolib.so': LIBCO_SHA256,
    })
    check_native(component, {'prepared-manifest.json': REACTOR13_MANIFEST_SHA256})
    manifest = json.loads((component / 'prepared-manifest.json').read_bytes())
    if (manifest['vendor_sha256'] != printer_sha256
            or manifest['libco_sha256'] != LIBCO_SHA256
            or set(manifest['files']) != set(FILES)):
        raise RuntimeError('Reactor .13 manifest does not match the selected profile')
    check_native(REACTOR_SOURCE, manifest['sources'])
    check_native(component / 'prepared', manifest['files'])
    check_native(component, {'firmware-launcher.sh': manifest['launcher_sha256']})
    destination = root / 'opt/inst/cc2-reactor'
    hook = root / 'etc/init.d/cc2-reactor-launch'
    if any(p.exists() or p.is_symlink() for p in (destination, hook)):
        raise RuntimeError('Existing Reactor installation in scratch RootFS')
    vendor = root / 'opt/bin/run_printer.sh'
    init = root / 'etc/init.d/printer'
    vendor_original = vendor.read_bytes()
    init_original = init.read_bytes()
    vendor_patched = patched_vendor_launcher(vendor_original.decode()).encode()
    init_patched = patched_printer_init(init_original.decode()).encode()
    destination.mkdir(parents=True)
    for name in FILES:
        target = destination / name
        target.write_bytes((component / 'prepared' / name).read_bytes())
        target.chmod(0o600 if name == 'SHA256SUMS' else 0o700)
    for name, data in (('run_printer.original.sh', vendor_original),
                       ('printer-init.original.sh', init_original)):
        target = destination / name
        target.write_bytes(data)
        target.chmod(0o600)
    hook.write_bytes((component / 'firmware-launcher.sh').read_bytes())
    hook.chmod(0o755)
    vendor.write_bytes(vendor_patched)
    vendor.chmod(0o755)
    init.write_bytes(init_patched)
    check_native(destination, manifest['files'])
    check_native(root, {'etc/init.d/cc2-reactor-launch': manifest['launcher_sha256']})


def stage_printer_extensions(root, component, reactor, printer_sha256, recovery_worker=None):
    """Add EXHAUST and the opt-in RFID guard to the qualified Reactor route."""
    receipt = json.loads((component / 'native13-check.json').read_bytes())
    modules = receipt['modules']
    if receipt['printer_sha256'] != printer_sha256 or set(modules) != set(PRINTER13_EXTENSIONS):
        raise RuntimeError('Printer extensions receipt does not match the .13 profile')
    check_native(component, modules)
    check_native(reactor, {'prepared-manifest.json': REACTOR13_MANIFEST_SHA256})
    manifest = json.loads((reactor / 'prepared-manifest.json').read_bytes())
    hook = root / 'etc/init.d/cc2-reactor-launch'
    check_native(hook.parent, {hook.name: manifest['launcher_sha256']})
    destination = root / 'opt/inst/cc2-printer-extensions13'
    if destination.exists() or destination.is_symlink():
        raise RuntimeError('Existing printer extensions in scratch RootFS')
    outer = hook.read_bytes()
    old = b'exec "$BASE/launcher.sh" "$@"\n'
    timeout = b'[ "$WAITED" -lt 120 ] || fallback "$@"'
    if (outer.count(old) != 1 or outer.count(b'fallback() {\n') != 1
            or outer.count(timeout) != 1):
        raise RuntimeError('Unexpected .13 Reactor launcher anchors')
    outer = outer.replace(b'fallback() {\n', shell_note().encode() + b'fallback() {\n'
                          b'    cc2_boot_note reactor "FALLBACK=native_printer reactor_preflight_refused"\n'
                          b'    for evidence in enabled lifecycle lifecycle.next; do\n'
                          b'        path=/opt/usr/cc2-rfid/$evidence\n'
                          b'        if [ -e "$path" ] || [ -L "$path" ]; then\n'
                          b'            echo "STOP: Canvas lifecycle requires the qualified Reactor route" >&2\n'
                          b'            exit 125\n        fi\n    done\n')
    disabled = b'[ ! -e /opt/usr/cc2-reactor-disabled ] || fallback "$@"'
    if outer.count(disabled) != 1:
        raise RuntimeError('Unexpected .13 Reactor disable marker anchor')
    outer = outer.replace(disabled,
        b'if [ -e /opt/usr/cc2-reactor-disabled ] || [ -L /opt/usr/cc2-reactor-disabled ]; then\n'
        b'    cc2_boot_note reactor "STOP=legacy_reactor_disable_marker prepare_upgrade_required"\n'
        b'    echo "STOP: legacy Reactor disable marker; complete firmware upgrade preparation" >&2\n'
        b'    exit 125\nfi')
    outer = outer.replace(old, b'exec /opt/inst/cc2-printer-extensions13/launch.sh "$@"\n')
    outer = outer.replace(timeout, b'[ "$WAITED" -lt 120 ] || exit 125')
    cavity, rfid = PRINTER13_EXTENSIONS
    canvas_gate = """for evidence in "$journal" "$pending" "$marker"; do
    [ ! -e "$evidence" ] && [ ! -L "$evidence" ] || stop
done
"""
    if recovery_worker is not None:
        worker = recovery_worker.read_bytes()
        if len(worker) < 20 or worker[:6] != b'\x7fELF\x01\x01' or worker[18:20] != b'\x28\x00':
            raise RuntimeError('Expected an ARM32 Canvas recovery worker')
        canvas_gate = f"""[ ! -e "$pending" ] && [ ! -L "$pending" ] || stop
if [ -e "$journal" ] || [ -L "$journal" ]; then
    [ -f "$journal" ] && [ ! -L "$journal" ] || stop
    recovery_worker=$source/recover-worker
    matches "$recovery_worker" {hashlib.sha256(worker).hexdigest()} || stop
    status=0
    "$recovery_worker" --boot-policy || status=$?
    if [ "$status" -eq 20 ]; then
        "$recovery_worker" --recover-original || stop
        status=0
        "$recovery_worker" --boot-policy || status=$?
        [ "$status" -eq 10 ] || stop
    fi
    case "$status" in
        0) ;;
        10)
            [ -f "$marker" ] && [ ! -L "$marker" ] || stop
            [ "$(cat "$marker")" = CC2-RFID-NATIVE-R2 ] || stop
            guard=/opt/usr/cc2-rfid/{rfid}
            matches "$guard" {modules[rfid]} || stop
            preloads=$preloads:$guard
            ;;
        *) stop;;
    esac
elif [ -e "$marker" ] || [ -L "$marker" ]; then
    stop
fi
"""
    launcher = f"""#!/bin/sh
set -eu
{shell_note()}
base=/opt/usr/cc2-reactor-runtime-v2
source=/opt/inst/cc2-printer-extensions13
native=$base/elegoo_printer
marker=/opt/usr/cc2-rfid/enabled
journal=/opt/usr/cc2-rfid/lifecycle
pending=/opt/usr/cc2-rfid/lifecycle.next
stop() {{ cc2_boot_note extensions 'STOP=printer_extension_qualification_failed';echo 'STOP: printer extension qualification failed' >&2; exit 125; }}
matches() {{
    [ -f "$1" ] && [ ! -L "$1" ] &&
        printf '%s  %s\\n' "$2" "$1" | sha256sum -c >/dev/null 2>&1
}}
[ -z "${{LD_PRELOAD:-}}" ] || stop
matches "$native" {printer_sha256} || stop
reactor=$base/libcc2-reactor-v2.so
matches "$reactor" {manifest['files']['libcc2-reactor-v2.so']} || stop
cavity=$source/{cavity}
matches "$cavity" {modules[cavity]} || stop
preloads=$reactor:$cavity
retire=/opt/inst/cc2-rfid13/retire-inactive
if [ -e "$retire" ] || [ -L "$retire" ]; then
    [ -f "$retire" ] && [ ! -L "$retire" ] || stop
    retired=$("$retire") || stop
    if [ -n "$retired" ]; then
        logger -t cc2-rfid "Canvas migration: $retired" || printf '%s\\n' "$retired"
    fi
fi
# CC2_CANVAS_GATE_BEGIN
{canvas_gate}# CC2_CANVAS_GATE_END
library_path=/opt/lib:/lib:/usr/lib
loaded=$(LD_LIBRARY_PATH="$library_path" LD_PRELOAD="$preloads" /lib/ld-linux-armhf.so.3 --list "$native") || stop
for module in $(printf '%s' "$preloads" | tr ':' ' '); do
    printf '%s\\n' "$loaded" | awk -v module="$module" '$1 == module || $3 == module {{ found=1 }} END {{ exit !found }}' || stop
done
cc2_boot_note extensions 'LAUNCH=verified_native_extensions'
CC2_REACTOR_RUNTIME_V2=1 LD_LIBRARY_PATH="$library_path" LD_PRELOAD="$preloads" exec "$native" "$@"
"""
    destination.mkdir()
    for name in PRINTER13_EXTENSIONS:
        target = destination / name
        target.write_bytes((component / name).read_bytes())
        target.chmod(0o600)
    check_native(destination, modules)
    if recovery_worker is not None:
        target = destination / 'recover-worker'
        target.write_bytes(worker)
        target.chmod(0o700)
        marker_template = destination / 'rfid-enabled.template'
        marker_template.write_bytes(b'CC2-RFID-NATIVE-R2\n')
        marker_template.chmod(0o600)
    entry = destination / 'launch.sh'
    entry.write_bytes(launcher.encode())
    entry.chmod(0o755)
    hook.write_bytes(outer)


def canvas_recovery_profile13(root, component, printer_sha256):
    """Bind a prospective .13 Canvas journal to the final immutable boot route."""
    receipt = json.loads((component / 'native13-check.json').read_bytes())
    rfid = PRINTER13_EXTENSIONS[1]
    if receipt['printer_sha256'] != printer_sha256 or set(receipt['modules']) != set(PRINTER13_EXTENSIONS):
        raise RuntimeError('Canvas recovery requires the exact .13 extensions profile')
    check_native(component, receipt['modules'])
    extension = '/opt/inst/cc2-printer-extensions13'
    paths = {
        'bootstrap': '/etc/init.d/cc2-reactor-launch',
        'entry': extension + '/launch.sh',
        'recover': extension + '/recover-worker',
        'marker': extension + '/rfid-enabled.template',
    }
    roles = {}
    for name, path in paths.items():
        item = root / path.lstrip('/')
        if item.is_symlink() or not item.is_file():
            raise RuntimeError('Missing regular .13 Canvas recovery role: ' + name)
        roles[name] = (hashlib.sha256(item.read_bytes()).hexdigest(), path)
    roles['native'] = (printer_sha256, '/opt/usr/cc2-reactor-runtime-v2/elegoo_printer')
    roles['guard'] = (receipt['modules'][rfid], '/opt/usr/cc2-rfid/' + rfid)
    if roles['marker'][0] != hashlib.sha256(b'CC2-RFID-NATIVE-R2\n').hexdigest():
        raise RuntimeError('Unexpected .13 Canvas marker template')
    return ''.join(f'{name} {digest} {path}\n' for name, (digest, path) in roles.items()).encode()
