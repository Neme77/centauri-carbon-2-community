"""Read the reviewed Connections runtime without rebuilding its kernel modules.

Only the pinned release input is accepted. Archive entries are checked before
any extraction; module and helper identities are independently pinned as well.
"""
from pathlib import Path, PurePosixPath
import hashlib
import io
import json
import re
import tarfile
import subprocess

REPO = Path(__file__).resolve().parents[3]
RUNTIME_SHA256 = '649d0af3dba7e21bc399fc741115fbe298cc1e4b77c07cf51bd35bfd73762e9a'
BINARY_PINS = {
    'control': '2a4dd2dd8e23aa3446348973ba6b8fa90a1fda16fcd96626015f8f0b816e9d62',
    'cc2_ue300_abi_probe.ko': '24c757788815abe5bbabb59d64b4768efedf7a65b1a8d93f3a6ce4d1e979a3f8',
    'cc2_ue300_abi_probe_3000.ko': '4027162b4a5a45fc136b932f4a33ee2a2f252270a0f7c29c6c03afc2d84e180a',
    'cc2_ue300_mii.ko': '9051751ccefa1c1795b644764fd4c2de31b54d871e863927ef17b6d65978a7be',
    'cc2_ue300_r8152.ko': '5118667293fe05753f38c31bafe654d110104c6d7239ba8c2cfa1d3151b83ebd',
    'libcc2-network-compat-r1.so': 'bde512195dd6bf3846f3103b5e1acbd89a62e5ee819e405389c97f5fc188ff3a',
}


def sha(data):
    return hashlib.sha256(data).hexdigest()


def read_runtime(path):
    path = Path(path)
    if path.is_symlink() or not path.is_file():
        raise RuntimeError('Missing regular qualified Connections archive')
    raw = path.read_bytes()
    if sha(raw) != RUNTIME_SHA256:
        raise RuntimeError('Connections archive SHA256 mismatch')
    pointer = json.loads((REPO / 'experimental/network-connections/runtime.json').read_text())
    if (pointer['sha256'] != RUNTIME_SHA256
            or pointer['helper_sha256'] != BINARY_PINS['control']):
        raise RuntimeError('Connections source and firmware input pins differ')
    files = {}
    with tarfile.open(fileobj=io.BytesIO(raw), mode='r:gz') as archive:
        total = 0
        for entry in archive:
            name = PurePosixPath(entry.name)
            if (not entry.isfile() or str(name) != entry.name or name.is_absolute()
                    or '..' in name.parts or not name.parts or '\\' in entry.name
                    or ':' in entry.name or entry.name in files
                    or entry.size < 0 or entry.size > 16 * 1024 * 1024):
                raise RuntimeError('Unsafe Connections archive entry')
            total += entry.size
            if total > 64 * 1024 * 1024:
                raise RuntimeError('Oversized Connections archive')
            files[entry.name] = archive.extractfile(entry).read()
    if 'SHA256SUMS' not in files:
        raise RuntimeError('Missing Connections checksum manifest')
    declared = {}
    for line in files['SHA256SUMS'].decode('ascii').splitlines():
        match = re.fullmatch(r'([a-f0-9]{64})  (.+)', line)
        if not match or match[2] in declared:
            raise RuntimeError('Invalid Connections checksum manifest')
        declared[match[2]] = match[1]
    if set(declared) != set(files) - {'SHA256SUMS'}:
        raise RuntimeError('Incomplete Connections checksum manifest')
    for name, digest in declared.items():
        if sha(files[name]) != digest:
            raise RuntimeError('Connections checksum mismatch: ' + name)
    for name, digest in BINARY_PINS.items():
        if name not in files or sha(files[name]) != digest:
            raise RuntimeError('Unqualified Connections binary: ' + name)
    return files


def stage_input(archive, destination):
    """Prepare ignored local input; no downloads, target firmware or printer I/O."""
    files = read_runtime(archive)
    destination = Path(destination)
    if destination.exists() or destination.is_symlink():
        raise RuntimeError('Connections prepared destination already exists')
    destination.mkdir(parents=True)
    for name, data in files.items():
        path = destination / name
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_bytes(data)
        path.chmod(0o700 if name == 'control' or name.endswith('.sh') else 0o600)
    return {'schema': 1, 'archive_sha256': RUNTIME_SHA256,
            'files': {name: sha(data) for name, data in sorted(files.items())},
            'kernel_rebuilt': False, 'modules_rebuilt': False,
            'firmware_boot_qualified': False}


NATIVE = {
    'bin/wifi_daemon': '8e19a620921c6f4f21bde910cc3d960af5400d223493041eb96f1e921dcbe3c1',
    'lib/modules/5.4.61/aic8800_fdrv.ko': '2973d7d7856b612ed5880fefa5b8f339d5bd882ff93f44ca47806c83a8a8d8c7',
    'etc/init.d/printer': 'e7537298955e23b7a03cab4c8d7bd241c1dfcc070883cdf51c4889cd255d1bc4',
    'etc/wifi/udhcpc_wlan0': 'b76f68e345f1221d835d1a0d6279827c5df4823c2e7fadfe0298cd49ec7bb7df',
    'usr/share/udhcpc/set_lan.sh': '53cf6af55ad333d5781b0d48d89fd48bde22bc516bd22e76d45f098dc224443c',
    'etc/init.d/cc2-camera-launch': 'f05accc97f0195cf4cf481b53b643902424183bb335a27f42e6069a625813c6f',
    'etc/init.d/cc2-reactor-launch': '8d4a19c9be1f6f85fe5ab9d3f116aa84400e7e6a77c455e66da636373fdaac43',
}
ROOT = 'opt/inst/cc2-connections'
MODULES = 'lib/modules/5.4.61/cc2'


def transform(program, data, mode=None):
    command = ['awk']
    if mode is not None:
        command += ['-v', 'mode=' + mode]
    command += [program.decode('utf-8')]
    result = subprocess.run(command, input=data, stdout=subprocess.PIPE,
                            stderr=subprocess.PIPE, timeout=10, check=True)
    if not result.stdout or b'\r' in result.stdout:
        raise RuntimeError('Invalid generated Connections startup delegate')
    return result.stdout


def hook_transform(text, mode, files):
    return transform(files['prepare-hooks.awk'], text.encode(), mode).decode()


def payload(root, files):
    root = Path(root)
    for name, expected in NATIVE.items():
        path = root / name
        if path.is_symlink() or not path.is_file() or sha(path.read_bytes()) != expected:
            raise RuntimeError('Connections firmware native profile differs: ' + name)
    result = dict(files)
    original = {
        'native-udhcpc_wlan0': 'etc/wifi/udhcpc_wlan0',
        'native-set_lan.sh': 'usr/share/udhcpc/set_lan.sh',
        'native-camera-launch': 'etc/init.d/cc2-camera-launch',
        'native-reactor-launch': 'etc/init.d/cc2-reactor-launch',
    }
    for name, path in original.items():
        result[name] = (root / path).read_bytes()
    for name, path, mode in (
            ('camera-entry.sh', 'opt/inst/cc2-camera-r2/boot-camera.sh', 'camera-entry'),
            ('reactor-entry.sh', 'opt/inst/cc2-reactor/launcher.sh', 'reactor-entry'),
            ('camera-launch.sh', 'etc/init.d/cc2-camera-launch', 'camera-launch'),
            ('reactor-launch.sh', 'etc/init.d/cc2-reactor-launch', 'reactor-launch')):
        result[name] = transform(files['prepare-hooks.awk'], (root / path).read_bytes(), mode)
    result['native-wan.sh'] = transform(files['native-wan-policy.awk'], result['native-set_lan.sh'])
    # Before UDISK seeding, native DHCP must still have a complete immutable
    # fallback. Do not dispatch to a missing/old persistent backup.
    for name, old, new in (
        ('wifi-client.sh', b'"$ROOT/native-udhcpc_wlan0"', b'/opt/inst/cc2-connections/native-udhcpc_wlan0'),
        ('native-lease.sh', b'"$ROOT/native-set_lan.sh"', b'/opt/inst/cc2-connections/native-set_lan.sh')):
        if result[name].count(old) != 1:
            raise RuntimeError('Unknown native Connections fallback')
        result[name] = result[name].replace(old, new)
    component = Path(__file__).resolve().parents[1] / 'components/connections'
    result['firmware-seed.sh'] = (component / 'firmware-seed.sh').read_bytes()
    result['init.sh'] = (component / 'firmware.init').read_bytes()
    hooks = {
        'etc/wifi/udhcpc_wlan0': 'wifi-client.sh',
        'usr/share/udhcpc/set_lan.sh': 'native-lease.sh',
        'etc/init.d/cc2-camera-launch': 'camera-launch.sh',
        'etc/init.d/cc2-reactor-launch': 'reactor-launch.sh',
        'etc/init.d/cc2-network-manager': 'init.sh',
        'etc/hotplug.d/net/90-cc2-network-manager': 'hotplug.sh',
    }
    startup = {path: sha(result[name]) for path, name in hooks.items()}
    startup['opt/usr/wifi/udhcpc_wlan0'] = sha(result['wifi-client.sh'])
    result['startup.sha256'] = ''.join(
        f'{digest}  /{name}\n' for name, digest in sorted(startup.items())).encode()
    result['SHA256SUMS'] = ''.join(f'{sha(data)}  {name}\n' for name, data in
                                  sorted(result.items()) if name != 'SHA256SUMS').encode()
    return result, hooks


def install(root, archive):
    root = Path(root)
    files = read_runtime(archive)
    prepared, hooks = payload(root, files)
    destination = root / ROOT
    link = root / 'etc/rc.d/S23cc2-network-manager'
    if destination.exists() or destination.is_symlink():
        raise RuntimeError('Firmware Connections destination already exists')
    if link.exists() or link.is_symlink():
        raise RuntimeError('Foreign Connections boot entry in stock RootFS')
    for name, data in prepared.items():
        if name.endswith('.sh'):
            if b'\r' in data:
                raise RuntimeError('Windows line endings in generated firmware shell source')
            if data.startswith(b'#!/usr/bin/env bash\n'):
                shell='bash'  # Retained build sources, never run on the printer.
            elif data.startswith((b'#!/bin/sh\n',b'#!/bin/sh /etc/rc.common\n')):
                shell='sh'
            else:
                raise RuntimeError('Unknown shell interpreter: ' + name)
            subprocess.run([shell,'-n'],input=data,check=True,timeout=10)
    for name, data in prepared.items():
        path = destination / name
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_bytes(data)
        path.chmod(0o700 if name == 'control' or name.endswith('.sh') else 0o600)
    for name in BINARY_PINS:
        if name.endswith('.ko'):
            path = root / MODULES / name
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_bytes(files[name]);path.chmod(0o644)
    for path, name in hooks.items():
        target = root / path
        target.parent.mkdir(parents=True, exist_ok=True)
        target.write_bytes(prepared[name]);target.chmod(0o755)
    init = root / 'etc/init.d/printer';init.chmod(0o755)
    link.symlink_to('../init.d/cc2-network-manager')
    return prepared, hooks


def audit(root, prepared, hooks):
    root = Path(root)
    for name in ('bin/wifi_daemon','lib/modules/5.4.61/aic8800_fdrv.ko'):
        path = root / name
        if path.is_symlink() or not path.is_file() or sha(path.read_bytes()) != NATIVE[name]:
            raise RuntimeError('Original Wi-Fi firmware component changed: ' + name)
    for name, data in prepared.items():
        path = root / ROOT / name
        if path.is_symlink() or not path.is_file() or path.read_bytes() != data:
            raise RuntimeError('Firmware Connections payload audit failed: ' + name)
        mode=0o700 if name == 'control' or name.endswith('.sh') else 0o600
        if path.stat().st_mode & 0o777 != mode:
            raise RuntimeError('Firmware Connections payload permissions differ: ' + name)
    for path, name in hooks.items():
        target = root / path
        if target.is_symlink() or not target.is_file() or target.read_bytes() != prepared[name]:
            raise RuntimeError('Firmware Connections hook audit failed: ' + path)
        if target.stat().st_mode & 0o777 != 0o755:
            raise RuntimeError('Firmware Connections hook permissions differ: ' + path)
    for name, digest in BINARY_PINS.items():
        if name.endswith('.ko'):
            path = root / MODULES / name
            if path.is_symlink() or not path.is_file() or sha(path.read_bytes()) != digest:
                raise RuntimeError('Firmware kernel module differs: ' + name)
            if path.stat().st_mode & 0o777 != 0o644:
                raise RuntimeError('Firmware kernel module permissions differ: ' + name)
    init = root / 'etc/init.d/printer'
    if sha(init.read_bytes()) != NATIVE['etc/init.d/printer'] or init.stat().st_mode & 0o777 != 0o755:
        raise RuntimeError('Native printer startup changed')
    link = root / 'etc/rc.d/S23cc2-network-manager'
    if not link.is_symlink() or str(link.readlink()) != '../init.d/cc2-network-manager':
        raise RuntimeError('Firmware Connections boot entry differs')
