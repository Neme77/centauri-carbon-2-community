"""Compile and qualify the .13 native components for ARM."""

import argparse
import hashlib
import json
from pathlib import Path
import re
import shutil
import subprocess
import sys
import tempfile


PORT = Path('/port')
SOURCE = PORT / 'source/experimental'
RFID = SOURCE / 'canvas-rfid/host'
EXHAUST = SOURCE / 'cavity-fan-floor'
CC = 'arm-linux-gnueabihf-gcc'
READELF = 'arm-linux-gnueabihf-readelf'
FLAGS = [
    '-O2', '-std=c11', '-Wall', '-Wextra', '-Wpedantic', '-Werror',
    '-D_POSIX_C_SOURCE=200809L', '-D_TIME_BITS=32', '-D_FILE_OFFSET_BITS=32',
    '-marm', '-mcpu=cortex-a7', '-mfpu=neon-vfpv4', '-mfloat-abi=hard',
    '-funwind-tables', '-pthread', '-DCC2_NATIVE_STOCK13',
]
SHARED = ['-fPIC', '-fvisibility=hidden', '-shared', '-Wl,-z,defs', '-Wl,-z,relro,-z,now']


def run(*args, capture=False):
    return subprocess.run(args, check=True, timeout=120, text=True,
                          stdout=subprocess.PIPE if capture else None).stdout


def verify_abi(module, readelf=READELF):
    header = run(readelf, '-h', module, capture=True)
    if 'ELF32' not in header or 'ARM' not in header or 'hard-float ABI' not in header:
        raise RuntimeError(f'{module.name}: expected ARM32 hard float')
    versions = run(readelf, '-V', module, capture=True)
    required = [tuple(map(int, v.split('.'))) for v in re.findall(r'GLIBC_([0-9.]+)', versions)]
    if not required or max(required) > (2, 23):
        raise RuntimeError(f'{module.name}: incompatible GLIBC versions\n{versions}')
    if 'TEXTREL' in run(readelf, '-d', module, capture=True):
        raise RuntimeError(f'{module.name}: text relocations')


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def connections_camera(out):
    port = Path(__file__).resolve().parent
    network = port / 'connections13-source'
    camera = port / 'source/experimental/camera-recovery-r2'
    prefix = '/opt/cc2-cross/toolchain-out/bin/arm-cortex_a15-linux-gnueabihf'
    cc = prefix + '-gcc'
    readelf = prefix + '-readelf'
    if run(cc, '-dumpversion', capture=True).strip() != '6.5.0':
        raise RuntimeError('Connections and Camera Recovery require the qualified GCC 6.5.0 toolchain')
    libc = run(cc, '-print-file-name=libc.so.6', capture=True).strip()
    if b'release version 2.23' not in Path(libc).read_bytes():
        raise RuntimeError('Connections and Camera Recovery require the qualified GLIBC 2.23 sysroot')
    flags = [flag for flag in FLAGS if flag not in ('-D_TIME_BITS=32', '-D_FILE_OFFSET_BITS=32')]
    manifest = json.loads((network / 'source-manifest.json').read_text())
    for name, digest in manifest['sources'].items():
        if sha(network / name) != digest:
            raise RuntimeError('Connections source mismatch: ' + name)
    if (sha(port / 'ai_camera.coexist') != manifest['camera_sha256']
            or sha(port / 'elegoo_printer.http-mqtt-pullback13') != manifest['printer_sha256']):
        raise RuntimeError('Connections executable profile mismatch')
    with tempfile.TemporaryDirectory(prefix='cc2-connections-camera13-') as scratch:
        root = Path(scratch)
        modules = {
            'libcc2-network-compat-r1.so': [network / name for name in
                ('network_compat.c', 'net_core.c', 'sha256_file.c')],
            'libcc2-camera-recovery-r2.so': [camera / name for name in
                ('camera_recovery.c', 'recovery_core.c', 'sha256_file.c')],
        }
        for name, sources in modules.items():
            module = root / name
            run(cc, *flags, *SHARED, *sources, '-o', module)
            verify_abi(module, readelf)
        sys.path.insert(0, str(port / 'source/builder/current/core'))
        from startup_stock13 import prepare
        startup = prepare(port / 'stock-startup')['etc/init.d/printer']
        init_digest = hashlib.sha256(startup).hexdigest()
        shim_digest = sha(root / 'libcc2-network-compat-r1.so')
        manager = root / 'control'
        run(cc, *flags, '-static',
            f'-DNG_PRINTER_INIT_SHA="{init_digest}"',
            f'-DNG_LIBCC2_NETWORK_COMPAT_R1_SO_SHA="{shim_digest}"',
            port / 'source/experimental/network-connections/manager.c', '-lm', '-o', manager)
        test = root / 'camera-recovery-test'
        run(cc, *flags, '-static', camera / 'test_recovery.c', camera / 'recovery_core.c',
            camera / 'sha256_file.c', '-o', test)
        out.mkdir()
        for name in modules:
            shutil.copyfile(root / name, out / name)
        shutil.copyfile(test, out / test.name).chmod(0o755)
        shutil.copyfile(manager, out / manager.name).chmod(0o755)
        result = {key: manifest[key] for key in ('camera_sha256', 'printer_sha256')}
        result['modules'] = {name: sha(out / name) for name in modules}
        result['test_sha256'] = sha(out / test.name)
        result['manager_sha256'] = sha(out / manager.name)
        result['printer_init_sha256'] = init_digest
        (out / 'legacy-build.json').write_text(json.dumps(result, indent=2) + '\n')
        print('PASS: .13 Connections and Camera Recovery compile and ABI')


def qualify_connections_camera(out):
    result = json.loads((out / 'legacy-build.json').read_text())
    for name in ('libcc2-network-compat-r1.so', 'libcc2-camera-recovery-r2.so'):
        module = out / name
        if sha(module) != result['modules'][name]:
            raise RuntimeError('Compiled module changed: ' + name)
        verify_abi(module)
    test = out / 'camera-recovery-test'
    if sha(test) != result['test_sha256']:
        raise RuntimeError('Camera Recovery test binary changed')
    run('qemu-arm', test)
    manager = out / 'control'
    if sha(manager) != result['manager_sha256']:
        raise RuntimeError('Connections manager binary changed')
    header = run(READELF, '-h', manager, capture=True)
    segments = run(READELF, '-l', manager, capture=True)
    if ('ELF32' not in header or 'ARM' not in header or 'hard-float ABI' not in header
            or 'INTERP' in segments):
        raise RuntimeError('Connections manager must be static ARM32 hard float')
    status = json.loads(run('qemu-arm', manager, '--status', capture=True))
    if status.get('temporary') is not False or status.get('automatic_selection') is not False:
        raise RuntimeError('Unexpected Connections manager status in clean QEMU fixture')
    print('PASS: .13 Connections manager ARM status')
    with (out / 'native13-check.json').open('x') as receipt:
        json.dump(result, receipt, indent=2)
        receipt.write('\n')
    print('PASS: .13 Connections and Camera Recovery ARM qualification:', out.name)


def main():
    profile = (SOURCE / 'firmware-port/native_profile13.h').read_text()
    expected = re.search(r'^#define CC2_NATIVE13_SHA256 "([0-9a-f]{64})"$',
                         profile, re.MULTILINE)
    printer_sha = sha(PORT / 'elegoo_printer.http-mqtt-pullback13')
    if not expected or printer_sha != expected[1]:
        raise RuntimeError('The .13 printer executable differs from the native module profile')
    with tempfile.TemporaryDirectory(prefix='cc2-native13-') as scratch:
        root = Path(scratch)
        modules = {
            'libcc2-rfid.so': [
                '-fno-strict-aliasing', '-DRF_REQUIRED_NATIVE=1',
                '-DRF_COMPONENT="/opt/usr/cc2-rfid"', RFID / 'native_module.c',
                RFID / 'rfid_guard.c',
            ],
            'libcc2-cavity-floor-r1.so': [EXHAUST / 'native_module.c', '-lm'],
        }
        for name, inputs in modules.items():
            module = root / name
            run(CC, *FLAGS, *SHARED, *inputs, '-o', module)
            verify_abi(module)
        for name, test in [('rfid', RFID / 'test_arm_patch.c'),
                           ('exhaust', EXHAUST / 'test_arm_abi.c')]:
            binary = root / name
            run(CC, *FLAGS, '-static', test, '-lm', '-o', binary)
            run('qemu-arm', binary)
        output = Path(tempfile.mkdtemp(prefix='qualified-', dir='/out'))
        for name in modules:
            shutil.copyfile(root / name, output / name)
        receipt = {'printer_sha256': printer_sha,
                   'modules': {name: sha(output / name) for name in modules}}
        (output / 'native13-check.json').write_text(json.dumps(receipt, indent=2) + '\n')
        print('PASS: .13 ARM RFID/EXHAUST module qualification:', output.name)


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('group', choices=('rfid-exhaust', 'connections-camera',
                                          'qualify-connections-camera'),
                        default='rfid-exhaust', nargs='?')
    parser.add_argument('--out', type=Path)
    args = parser.parse_args()
    if args.group != 'rfid-exhaust' and args.out is None:
        parser.error('--out is required for Connections and Camera Recovery')
    if args.group == 'connections-camera':
        connections_camera(args.out)
    elif args.group == 'qualify-connections-camera':
        qualify_connections_camera(args.out)
    else:
        main()
