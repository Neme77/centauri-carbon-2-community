"""Stage the verified .13 display resources and binary patches in a scratch RootFS."""

import argparse
import hashlib
import importlib.util
from pathlib import Path
import subprocess
import sys
from tempfile import TemporaryDirectory

from firmware_builder import (
    EXPECTED, PORT_DIR, PORT_HASHES, SSH_FILES, audit_cc2_control, audit_ssh,
    install_cc2_control, install_ssh, load_cc2_control, reqhash,
)
from native_display_component import SOURCE, apply_palette13, install_images
from startup_stock13 import (
    prepare, stage_camera_recovery, stage_printer_extensions, stage_reactor,
    verify_qualified_component,
)


GUI_HOME_NETWORK_SHA256 = '32818c65c4cbbcd7b3cb74054505ff6907c226febacfd5c0081091ce97f4908a'
PRINTER_NETWORK13_SHA256 = 'ee48e7ee3bb5c6bbd2f6ea780e372f168ed9bd5a3f85eb5cf3f70e726271ba0e'
PRINTER_PULLBACK13_SHA256 = '768a9e8c307d2398dc486be3a3ca8dfa6e9265677627d133f03b6914a1a34e59'
DISPLAY_PORT_INPUTS = {
    'port_home13.py': '356c4b13382c39cb3cec02cd2f020b2415170c0bc62053810e7bf34fa764e5d0',
    'port_network13.py': '7ccecf0afaaa374bc91b2a1ee402f78eee4ca50244fcc75329f1e113b85cdd91',
    'audit_stock13.py': 'b10c1f003cc6af612de6611a9815775223d78a39203909e70e686e7c98a7d87f',
    'elf_layout.py': '9d1c016043f5a29133b7285c128d986e1c933281b89818df0b93c6b883dc3a82',
    'home-proof.json': 'fe2381bfc023a852a70723b929772806da9b34aa18e0184bfdd2f52ba5c4441f',
    'network-proof.json': '3b0842862018d59fb5fd35cd2fc1c9716bc6a3c3532f2dbbb2241ef5d19159a8',
    'patch-plan.json': 'ade9b489f067dc3e1eaab3cbff99144a27304975087b077d3e0d9748d55e7ab5',
}


def stage_home_network(stock07, palette13, scratch):
    for name, digest in DISPLAY_PORT_INPUTS.items():
        reqhash(SOURCE / name, digest, '.13 display port input ' + name)
    reqhash(PORT_DIR / 'map_printer13.py',
            '868d92aba4c4ba0db56157f60b74bf8773f82ff1f095b240c846616bd93ca1d3',
            '.13 ELF symbol reader')
    reqhash(stock07, '24d0e561287a4b5a87c8ec698c98f60acf2c70511114b36224b4eb9249283abd',
            '.07 stock GUI reference')
    palette = scratch / 'gui-palette'
    output = scratch / 'gui-home-network'
    palette.write_bytes(palette13)
    subprocess.run([sys.executable, '-E', '-s', '-B', str(SOURCE / 'port_home13.py'),
                    str(stock07.resolve()), str(palette), str(output)],
                   check=True, timeout=60)
    reqhash(output, GUI_HOME_NETWORK_SHA256, '.13 Home and Network GUI')
    return output.read_bytes()


def set_boot_animation_timings(gui):
    if hashlib.sha256(gui).hexdigest() != GUI_HOME_NETWORK_SHA256:
        raise RuntimeError('Unexpected .13 GUI for boot animation timings')
    default, themed = 0x17020, 0x171f4
    if (gui[default:default + 4] != bytes.fromhex('981a03e3') or
            gui[themed:themed + 4] != bytes.fromhex('882301e3')):
        raise RuntimeError('Unexpected .13 boot animation instructions')
    # Keep the standard animation at 15 s; set the themed bar to 15 s.
    print('PASS: .13 boot animations set to 15000 ms standard and 15000 ms themed')
    return gui[:themed] + bytes.fromhex('982a03e3') + gui[themed + 4:]


def load_patcher(name):
    filename = name + '.py'
    path = PORT_DIR / filename
    reqhash(path, PORT_HASHES[filename], '.13 patcher ' + filename)
    spec = importlib.util.spec_from_file_location(name, path)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def stage(root, app=None, ssh_dir=None, network_trial=False, *, stock07_gui=None,
          pullback_trial=False, reactor_component=None, camera_recovery_component=None,
          printer_extensions=None, connections_runtime=None, recovery_worker=None):
    if pullback_trial and not network_trial:
        raise ValueError('The .13 pullback trial requires the HTTP/MQTT trial')
    if reactor_component is not None and not network_trial:
        raise ValueError('The reviewed .13 Reactor build requires the HTTP/MQTT profile')
    if camera_recovery_component is not None and reactor_component is None:
        raise ValueError('The reviewed .13 Camera Recovery build requires Reactor staging')
    if printer_extensions is not None and reactor_component is None:
        raise ValueError('The reviewed .13 printer extensions require Reactor staging')
    if recovery_worker is not None and printer_extensions is None:
        raise ValueError('The .13 recovery worker requires printer extensions staging')
    if connections_runtime is not None and (
            stock07_gui is None or camera_recovery_component is None or printer_extensions is None):
        raise ValueError('The .13 Connections runtime requires Home/Network, Camera Recovery and printer extensions')
    root = Path(root)
    prepare(root)
    ssh_inputs = []
    if ssh_dir is not None:
        for filename, (_, pin, _) in SSH_FILES.items():
            source = ssh_dir / filename
            reqhash(source, EXPECTED[pin], '.13 SSH ' + filename)
            ssh_inputs.append(source)
    gui = root / 'opt/bin/ec-eeb001-gui'
    camera = root / 'opt/bin/ai_camera'
    daemon = root / 'opt/inst/daemon-000/daemon-000'
    reqhash(daemon, EXPECTED['daemon_stock'], '.13 stock OTA daemon')
    for filename in ('dual44-manifest.json', 'dual_verify_44.bin'):
        reqhash(PORT_DIR / filename, PORT_HASHES[filename], '.13 Dual Trust ' + filename)
    patched_daemon = load_patcher('patch_dual44').patch(daemon.read_bytes())
    reqhash(gui, '8d6cac2aecb26b0e504b9d1b71a8b5bb9168fc14143eb0e8fcfc5d7d8f2507fc',
            '.13 stock GUI')
    stock_gui = gui.read_bytes()
    patched_gui = load_patcher('patch_zoffset44').patch(stock_gui)
    with TemporaryDirectory() as temporary:
        temp = Path(temporary)
        gui_candidate = temp / 'gui'
        gui_candidate.write_bytes(patched_gui)
        reqhash(gui_candidate, '668ab8cb490c74eb5bbd769b421f6e4ade4e91fba322e22257496b9645f9ebfe',
                '.13 Z-offset GUI')
        patched_gui = apply_palette13(patched_gui)
        if stock07_gui is not None:
            patched_gui = stage_home_network(stock07_gui, patched_gui, temp)
        camera_candidate = temp / 'camera'
        reqhash(PORT_DIR / 'camera13-manifest.json',
                'd40398746995ddae595f7a49b3c769f07339bfb4d87044f6c9b670a7209df722',
                '.13 camera manifest')
        load_patcher('patch_camera44').apply(camera, camera_candidate, 'camera13-manifest.json')
        reqhash(camera_candidate, '90da7634c21b74b3bf943d68129da78668b47f9706620e158283697ab95553a7',
                '.13 LAN/WAN camera')
        daemon_candidate = temp / 'daemon'
        daemon_candidate.write_bytes(patched_daemon)
        reqhash(daemon_candidate, EXPECTED['daemon_dual'], '.13 Dual Trust daemon')
        if network_trial:
            printer = root / 'opt/bin/elegoo_printer'
            http = temp / 'printer-http'
            network = temp / 'printer-network'
            for name, digest in (
                ('http13-manifest.json', '1791d00ef2638bae277de8bf93c09ad1f0df9ec299e9f7dddc29ff25c717c2c4'),
                ('network13-manifest.json', '770af56db5b8ff0547ff2f65c592c5b4f32185eb676a16571757ec210341e08f'),
            ):
                reqhash(PORT_DIR / name, digest, '.13 network trial manifest')
            load_patcher('patch_http44').apply(printer, http, stock13=True)
            load_patcher('patch_network44').apply(http, network, stock13=True)
            reqhash(network, PRINTER_NETWORK13_SHA256, '.13 HTTP/MQTT trial executable')
            patcher = PORT_DIR.parent / 'canvas-pullback-1000/patch_pullback.py'
            reqhash(patcher, '69cb9b99207aae9e4382fda1ea7b1c8a06dd0722bf8a0505a5b3802824b5afae',
                    '.13 pullback patcher')
            spec = importlib.util.spec_from_file_location('patch_pullback13', patcher)
            module = importlib.util.module_from_spec(spec)
            spec.loader.exec_module(module)
            network.write_bytes(module.transform(network.read_bytes(), stock13=True))
            reqhash(network, PRINTER_PULLBACK13_SHA256, '.13 executable with 1000 mm pullback support')
        if camera_recovery_component is not None:
            modules = verify_qualified_component(
                camera_recovery_component, camera_candidate, network, PRINTER_PULLBACK13_SHA256)
        if ssh_inputs:
            install_ssh(root, *ssh_inputs)
            audit_ssh(root)
        install_images(root)
        gui.write_bytes(patched_gui)
        camera.write_bytes(camera_candidate.read_bytes())
        daemon.write_bytes(patched_daemon)
        if network_trial:
            printer.write_bytes(network.read_bytes())
        if reactor_component is not None:
            stage_reactor(root, reactor_component, PRINTER_PULLBACK13_SHA256)
        if printer_extensions is not None:
            stage_printer_extensions(root, printer_extensions, reactor_component,
                                     PRINTER_PULLBACK13_SHA256, recovery_worker)
        if camera_recovery_component is not None:
            stage_camera_recovery(root, camera_recovery_component, modules)
        if connections_runtime is not None:
            from stage_connections13 import stage as stage_connections
            stage_connections(root, connections_runtime, camera_recovery_component,
                              reactor_component, printer_extensions)
        if app is not None:
            component, manifest = app
            install_cc2_control(root, component, manifest)
            audit_cc2_control(root, component, manifest)
        if stock07_gui is not None:
            gui.write_bytes(set_boot_animation_timings(gui.read_bytes()))


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('scratch_rootfs', type=Path)
    parser.add_argument('--cc2-control', nargs=2, type=Path, metavar=('PREPARED', 'MANIFEST'))
    parser.add_argument('--ssh-dir', type=Path)
    parser.add_argument('--stock07-gui', type=Path,
                        help='Build Home and Network using the verified stock .07 GUI reference')
    parser.add_argument('--network-trial', action='store_true',
                        help='Stage the HTTP/MQTT candidate with 1000 mm Canvas pullback support')
    parser.add_argument('--pullback-trial', action='store_true',
                        help='Legacy option. The HTTP/MQTT candidate already includes pullback support')
    parser.add_argument('--reactor-component', type=Path,
                        help='Include the exact prepared .13 Reactor component')
    parser.add_argument('--camera-recovery-component', type=Path,
                        help='Include Camera Recovery from the qualified .13 ARM run directory')
    parser.add_argument('--printer-extensions', type=Path,
                        help='Stage qualified .13 EXHAUST and the opt-in Canvas RFID guard')
    parser.add_argument('--connections-runtime', type=Path,
                        help='Stage the pinned LAN archive with the qualified .13 manager and launchers')
    args = parser.parse_args()
    app = load_cc2_control(*args.cc2_control) if args.cc2_control else None
    stage(args.scratch_rootfs, app, args.ssh_dir, args.network_trial,
          stock07_gui=args.stock07_gui, pullback_trial=args.pullback_trial,
          reactor_component=args.reactor_component,
          camera_recovery_component=args.camera_recovery_component,
          printer_extensions=args.printer_extensions, connections_runtime=args.connections_runtime)
    print('PASS: .13 graphics, palette, Z-offset, LAN/WAN and Dual Trust staged')
    if args.stock07_gui:
        print('PASS: .13 Home and Network GUI staged')
    if app:
        print('PASS: CC2 Control ' + app[1]['version'] + ' staged')
    if args.network_trial:
        print('PASS: .13 HTTP/MQTT and 1000 mm Canvas pullback staged')
    if args.reactor_component:
        print('PASS: .13 Reactor component staged')
    if args.camera_recovery_component:
        print('PASS: .13 Camera Recovery boot hook staged')
    if args.printer_extensions:
        print('PASS: .13 EXHAUST and opt-in RFID boot hook staged')
    if args.connections_runtime:
        print('PASS: .13 Connections runtime and combined boot hooks staged')
