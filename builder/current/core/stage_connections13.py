"""Install the LAN trial runtime into a .13 Linux build tree after native launchers."""

import argparse
import json
from pathlib import Path

from connections_component import audit, check_native, install, NATIVE, STOCK_13, read_runtime, sha
from firmware_builder import reqhash
from stage_stock13 import GUI_HOME_NETWORK_SHA256, PRINTER_PULLBACK13_SHA256
from startup_stock13 import (
    CAMERA13_SHA256, CAMERA_RECOVERY13, PRINTER13_EXTENSIONS, REACTOR13_MANIFEST_SHA256,
    verify_qualified_component,
)


def verify_manager(component, init):
    """Bind the qualified Connections manager to the exact staged init script."""
    component = Path(component)
    receipt = json.loads((component / 'native13-check.json').read_bytes())
    if not receipt.get('manager_sha256') or not receipt.get('printer_init_sha256'):
        raise RuntimeError('Missing .13 Connections manager qualification')
    check_native(component, {
        'control': receipt['manager_sha256'],
        'libcc2-network-compat-r1.so': receipt['modules']['libcc2-network-compat-r1.so'],
    })
    check_native(init.parent, {init.name: receipt['printer_init_sha256']})


def prepare_profile(root, archive, component, reactor):
    """Generate the read-only Connections profile for the combined .13 RootFS."""
    root, reactor = Path(root), Path(reactor)
    verify_manager(component, root / 'etc/init.d/printer')
    modules = verify_qualified_component(component, root / 'opt/bin/ai_camera',
                                         root / 'opt/bin/elegoo_printer',
                                         PRINTER_PULLBACK13_SHA256)
    check_native(reactor, {'prepared-manifest.json': REACTOR13_MANIFEST_SHA256})
    manifest = json.loads((reactor / 'prepared-manifest.json').read_bytes())
    reactor_files = manifest['files']
    startup_pin = NATIVE['etc/init.d/printer']
    recovery = 'opt/inst/cc2-camera-recovery13/' + CAMERA_RECOVERY13
    check_native(root, {
        'etc/init.d/printer': startup_pin,
        'opt/lib/libcolib.so': manifest['libco_sha256'],
        'opt/inst/cc2-reactor/launcher.sh': reactor_files['launcher.sh'],
        'opt/inst/cc2-reactor/libcc2-reactor-v2.so': reactor_files['libcc2-reactor-v2.so'],
        recovery: modules[CAMERA_RECOVERY13],
    })
    profile = read_runtime(archive, lan_preview=True)['profile.sh'].decode()
    replacements = {
        'ec5633b17de329d5b18c3f8d8de6ebf389391a04193536d976ba167022d07b99': CAMERA13_SHA256,
        '19712b5943b10bb8cb6aab9a4ed498ee962b28538b23c91b7ed003eb9e590745': PRINTER_PULLBACK13_SHA256,
        'a260865964759898cf47ed8e30e20eb8981506c1a22ada32611b9911b36b47fc': reactor_files['launcher.sh'],
        '4639364c68489ec40c4e0c4f425550f3aa19e44c2ee61ed641affdc560e7e92c': reactor_files['libcc2-reactor-v2.so'],
    }
    for old, new in replacements.items():
        if profile.count(old) != 1:
            raise RuntimeError('Unexpected Connections profile fingerprint anchor: ' + old)
        profile = profile.replace(old, new)
    start = profile.index("for item in 'libcc2-camera-reap-r2.so ")
    end = profile.index('\ndone\n', start) + len('\ndone\n')
    digest = modules[CAMERA_RECOVERY13]
    check = f'[ "$(sha /{recovery})" = {digest} ] || fail camera_component_profile\n'
    return (profile[:start] + check + profile[end:]).encode()


def prepare_activation(archive, component, extensions):
    """Prepare boot-only activation while the combined printer launcher is under test."""
    component = Path(component)
    receipt = json.loads((component / 'native13-check.json').read_bytes())
    shim = 'libcc2-network-compat-r1.so'
    digest = receipt['modules'][shim]
    if (receipt['camera_sha256'] != CAMERA13_SHA256
            or receipt['printer_sha256'] != PRINTER_PULLBACK13_SHA256):
        raise RuntimeError('Connections activation requires the combined .13 profile')
    check_native(component, {shim: digest})
    extension_receipt = json.loads((extensions / 'native13-check.json').read_bytes())
    extension_pins = extension_receipt['modules']
    if (extension_receipt['printer_sha256'] != PRINTER_PULLBACK13_SHA256
            or set(extension_pins) != set(PRINTER13_EXTENSIONS)):
        raise RuntimeError('Connections activation requires the qualified .13 printer extensions')
    check_native(extensions, extension_pins)
    cavity, rfid = PRINTER13_EXTENSIONS
    printer_profile = f'''CAVITY=/opt/inst/cc2-printer-extensions13/{cavity}
    RFID=/opt/usr/cc2-rfid/{rfid}
    MARKER=/opt/usr/cc2-rfid/enabled
    expected_preloads=${{network_preload}}$REACTOR:$CAVITY
    [ -f "$CAVITY" ] && [ ! -L "$CAVITY" ] || fail exhaust_library
    [ "$(sha256sum "$CAVITY" | awk '{{print $1}}')" = {extension_pins[cavity]} ] || fail exhaust_library
    grep -F "$CAVITY" "/proc/$printer/maps" >/dev/null || fail exhaust_mapping
    if [ -e "$MARKER" ] || [ -L "$MARKER" ]; then
        [ -f "$MARKER" ] && [ ! -L "$MARKER" ] || fail rfid_marker
        [ "$(cat "$MARKER")" = CC2-RFID-NATIVE-R2 ] || fail rfid_marker
        [ -f "$RFID" ] && [ ! -L "$RFID" ] || fail rfid_library
        [ "$(sha256sum "$RFID" | awk '{{print $1}}')" = {extension_pins[rfid]} ] || fail rfid_library
        grep -F "$RFID" "/proc/$printer/maps" >/dev/null || fail rfid_mapping
        expected_preloads=$expected_preloads:$RFID
    fi
    [ "$p" = "$expected_preloads" ] || fail printer_preload'''
    legacy = read_runtime(archive, lan_preview=True)['activate.sh'].decode()
    script = legacy[:legacy.index('stop_one(){')]
    script = script.replace('restart native services only if needed', 'verify the boot-loaded native services')
    script = script.replace('# The independent configuration watchdog must already be running for start.\n', '')
    for line in (
        'OLD=/tmp/cc2-network-compat-r1/libcc2-network-compat-r1.so',
        'REAP=/opt/usr/cc2-control/camera/libcc2-camera-reap-r2.so',
        'STATE=/opt/usr/cc2-network-state',
    ):
        script = script.replace(line + '\n', '')
    replacements = {
        'RECOVERY=/opt/usr/cc2-control/camera/libcc2-camera-recovery-r2.so':
            'RECOVERY=/opt/inst/cc2-camera-recovery13/' + CAMERA_RECOVERY13,
        'ec5633b17de329d5b18c3f8d8de6ebf389391a04193536d976ba167022d07b99': CAMERA13_SHA256,
        '19712b5943b10bb8cb6aab9a4ed498ee962b28538b23c91b7ed003eb9e590745': PRINTER_PULLBACK13_SHA256,
        'case "$c" in "$REAP:$RECOVERY"|"$OLD:$REAP:$RECOVERY"|"$LIB:$REAP:$RECOVERY") ;;*) fail camera_preload;;esac':
            'case "$c" in "$RECOVERY") network_preload=;; "$LIB:$RECOVERY") network_preload=$LIB:;; *) fail camera_preload;;esac',
        'case "$p" in "$REACTOR"|"$OLD:$REACTOR"|"$LIB:$REACTOR") ;;*) fail printer_preload;;esac':
            printer_profile,
        'for flag in CC2_CAMERA_REAP_TRIAL CC2_CAMERA_REAP_ENABLE_LAN CC2_CAMERA_RECOVERY_TRIAL;do [ "$(env_field "$camera" "$flag")" = 1 ] || fail camera_flags;done':
            '[ "$(env_field "$camera" CC2_CAMERA_RECOVERY_TRIAL)" = 1 ] || fail camera_flags',
        'for item in "$REAP" "$RECOVERY";do grep -F "$item" "/proc/$camera/maps" >/dev/null || fail camera_mapping;done':
            'grep -F "$RECOVERY" "/proc/$camera/maps" >/dev/null || fail camera_mapping',
        'for lib in "$LIB" "$OLD";do':
            'for lib in "$LIB";do',
        'bde512195dd6bf3846f3103b5e1acbd89a62e5ee819e405389c97f5fc188ff3a': digest,
        "grep -F 'libcc2-network-compat-r1.so'":
            'grep -F "$LIB"',
    }
    for old, new in replacements.items():
        if script.count(old) != 1:
            raise RuntimeError('Unexpected Connections activation anchor: ' + old)
        script = script.replace(old, new)
    return (script + """case "${1:-}" in
 ready|start)
    sh "$ROOT/profile.sh" ethernet || fail platform_profile
    if [ "$1" = start ]; then
        /opt/inst/cc2-control/cc2-control --installer-native-idle || fail printer_not_ready
    fi
    qualify
    if [ "$1" = start ] && [ -z "$network_preload" ]; then
        fail connections_requires_full_idle_reboot
    fi
    ready || fail runtime_not_confirmed
    [ "$1" = ready ] || echo 'activation:already_ready'
    ;;
 repair) exit 0;; # This activation path never stops a native process.
 *) fail unknown_action;;
esac
""").encode()


def prepare_boot_delegates(root, receipt):
    camera = 'etc/init.d/cc2-camera-launch'
    reactor = 'etc/init.d/cc2-reactor-launch'
    printer = 'opt/inst/cc2-printer-extensions13/launch.sh'
    control_sha = receipt['manager_sha256']
    shim_sha = receipt['modules']['libcc2-network-compat-r1.so']
    common = b'. /opt/inst/cc2-connections/native-preload.sh\n'
    helper = f"""#!/bin/sh
# Sourced before either native process receives LD_PRELOAD.
network_control=/opt/usr/cc2-network-manager/control
network_library=/opt/usr/cc2-network-manager/libcc2-network-compat-r1.so
network_enabled=0
if [ -f "$network_control" ] && [ ! -L "$network_control" ] &&
    [ -f "$network_library" ] && [ ! -L "$network_library" ] &&
    printf '%s  %s\\n' '{control_sha}' "$network_control" '{shim_sha}' "$network_library" |
        sha256sum -c >/dev/null 2>&1 &&
    "$network_control" --enabled; then
    "$network_control" --ensure || true
    network_enabled=1
fi
""".encode()

    def insert(path, before, after):
        data = (root / path).read_bytes()
        if data.count(before) != 1:
            raise RuntimeError('Unexpected .13 Connections boot anchor: ' + path)
        return data.replace(before, after)

    camera_data = insert(camera,
        b'    CC2_CAMERA_RECOVERY_TRIAL=1 LD_PRELOAD="$recovery" exec "$camera" "$@"\n',
        b'    ' + common +
        b'    if [ "$network_enabled" = 1 ]; then\n'
        b'        "$network_control" --wait-camera-link || true\n'
        b'        recovery=$network_library:$recovery\n'
        b'    fi\n'
        b'    CC2_NETWORK_COMPAT_TRIAL="$network_enabled" CC2_CAMERA_RECOVERY_TRIAL=1 LD_PRELOAD="$recovery" exec "$camera" "$@"\n')
    printer_data = insert(printer, b'library_path=/opt/lib:/lib:/usr/lib\n',
        common + b'[ "$network_enabled" = 0 ] || preloads=$network_library:$preloads\n'
        b'library_path=/opt/lib:/lib:/usr/lib\n')
    before = b'CC2_REACTOR_RUNTIME_V2=1 LD_LIBRARY_PATH='
    if printer_data.count(before) != 1:
        raise RuntimeError('Unexpected .13 printer exec anchor')
    printer_data = printer_data.replace(before, b'CC2_NETWORK_COMPAT_TRIAL="$network_enabled" ' + before)
    reactor_data = (root / reactor).read_bytes()
    if reactor_data.count(b'exec /opt/inst/cc2-printer-extensions13/launch.sh "$@"') != 1:
        raise RuntimeError('Connections requires the combined .13 Reactor route')
    return helper, {
        camera: ('camera-launch.sh', camera_data),
        reactor: ('reactor-launch.sh', reactor_data),
        printer: ('printer-launch.sh', printer_data),
    }


def stage(root, archive, component, reactor, extensions):
    root = Path(root)
    gui = root / 'opt/bin/ec-eeb001-gui'
    reqhash(gui, GUI_HOME_NETWORK_SHA256, '.13 Home and Network GUI before Connections')
    component = Path(component)
    profile = prepare_profile(root, archive, component, reactor)
    activation = prepare_activation(archive, component, Path(extensions))
    receipt = json.loads((component / 'native13-check.json').read_bytes())
    helper, delegates = prepare_boot_delegates(root, receipt)
    native = {**STOCK_13, 'etc/init.d/printer': receipt['printer_init_sha256']}
    overrides = {
        'control': (component / 'control').read_bytes(),
        'libcc2-network-compat-r1.so': (component / 'libcc2-network-compat-r1.so').read_bytes(),
        'profile.sh': profile,
        'activate.sh': activation,
        'native-preload.sh': helper,
    }
    prepared, hooks = install(root, archive, lan_preview=True, native=native,
                              overrides=overrides, boot_delegates=delegates)
    audit(root, prepared, hooks, native=native)
    reqhash(gui, GUI_HOME_NETWORK_SHA256, '.13 Home and Network GUI after Connections')


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('rootfs', type=Path)
    parser.add_argument('runtime', type=Path)
    parser.add_argument('--prepare-profile', nargs=3, type=Path,
                        metavar=('QUALIFIED_ARM_DIR', 'REACTOR_DIR', 'OUTPUT'))
    parser.add_argument('--prepare-activation', nargs=3, type=Path,
                        metavar=('QUALIFIED_ARM_DIR', 'EXTENSIONS_DIR', 'OUTPUT'))
    parser.add_argument('--components', nargs=3, type=Path,
                        metavar=('QUALIFIED_ARM_DIR', 'REACTOR_DIR', 'EXTENSIONS_DIR'))
    args = parser.parse_args()
    if args.prepare_activation:
        component, extensions, output = args.prepare_activation
        activation = prepare_activation(args.runtime, component, extensions)
        with output.open('xb') as handle:
            handle.write(activation)
        print('PASS: .13 boot-only Connections activation prepared, SHA256=' + sha(activation))
        raise SystemExit(0)
    if args.prepare_profile:
        component, reactor, output = args.prepare_profile
        profile = prepare_profile(args.rootfs, args.runtime, component, reactor)
        with output.open('xb') as handle:
            handle.write(profile)
        print('PASS: .13 Connections profile prepared, SHA256=' + sha(profile))
        raise SystemExit(0)
    if not args.components:
        parser.error('--components is required for .13 Connections staging')
    stage(args.rootfs, args.runtime, *args.components)
    print('PASS: .13 Connections runtime, modules and boot hooks staged')
