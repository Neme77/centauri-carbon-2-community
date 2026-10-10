#!/usr/bin/env python3
"""Build, validate and stage CC2 Control for the current community firmware."""
import hashlib
import json
from pathlib import Path
import shutil
import struct
import subprocess
import sys
import tempfile
import zipfile

ROOT = Path(__file__).resolve().parent
COMPONENT = ROOT / 'components/cc2-control'
SOURCE = COMPONENT / 'source/source.zip'
SOURCE_COMMIT='83759fb91cc42c37433776cb77967456f34bda1c'
SOURCE_SHA256='d8e86c85fb77c17f012affb382a328b35ba387c0e93cd89b93ecb9e74ada4181'
RUNTIME = COMPONENT / 'runtime'
OUTPUT = COMPONENT / 'prepared'
MANIFEST = COMPONENT / 'prepared-manifest.json'
# The version is written once, in cc2-control/VERSION, and travels inside the pinned snapshot.
with zipfile.ZipFile(SOURCE) as _snapshot:
    VERSION = _snapshot.read('VERSION').decode().strip()

def sha256(path):
    h = hashlib.sha256()
    with path.open('rb') as src:
        for block in iter(lambda: src.read(1024 * 1024), b''):
            h.update(block)
    return h.hexdigest()

def validate_arm_elf(path):
    data = path.read_bytes()[:64]
    if len(data) < 20 or data[:4] != b'\x7fELF' or data[4] != 1 or data[5] != 1:
        raise RuntimeError('CC2 Control output is not a 32-bit little-endian ELF')
    if struct.unpack_from('<H', data, 18)[0] != 40:
        raise RuntimeError('CC2 Control output is not an ARM executable')
    if VERSION.encode() not in path.read_bytes():
        raise RuntimeError(f'CC2 Control binary does not identify version {VERSION}')

def copy_shell_lf(source, destination):
    data = source.read_bytes()
    if data.startswith(b'\xef\xbb\xbf'):
        data = data[3:]
    data = data.replace(b'\r\n', b'\n').replace(b'\r', b'\n')
    if not data.startswith(b'#!'):
        raise RuntimeError(f'Shell script has no shebang: {source}')
    destination.write_bytes(data)
    destination.chmod(0o755)

def prepared_mode(relative):
    return 0o755 if relative in ('cc2-control', 'start.sh', 'launch.sh', 'cc2-control.init', 'cc2-configure') else 0o644

def main():
    firmware_init = (RUNTIME / 'cc2-control.init').read_text(encoding='utf-8')
    if '/opt/inst/cc2-control/start.sh' not in firmware_init or '/opt/usr/cc2-control/launch.sh' in firmware_init:
        raise RuntimeError('Firmware init must launch /opt/inst/cc2-control/start.sh')
    if sha256(SOURCE) != SOURCE_SHA256:
        raise RuntimeError('CC2 Control source archive hash mismatch')
    if OUTPUT.exists():
        shutil.rmtree(OUTPUT)
    with tempfile.TemporaryDirectory(prefix='cc2-control-prepare-') as temporary:
        tree = Path(temporary) / 'source'
        tree.mkdir()
        with zipfile.ZipFile(SOURCE) as archive:
            archive.extractall(tree)
        # The release snapshot contains the standalone launcher in its overlay.
        # Replace it with the validated persistent-storage launcher before tests
        # and packaging. The runtime copy is the canonical firmware launcher.
        firmware_start = tree / 'firmware-integration/overlay/opt/inst/cc2-control/start.sh'
        copy_shell_lf(RUNTIME / 'start.sh', firmware_start)
        subprocess.run(['make', 'clean', 'test', 'CROSS=', 'CC=gcc'], cwd=tree, check=True)
        compiler='/opt/cc2-cross/toolchain-out/bin/arm-cortex_a15-linux-gnueabihf-gcc'
        subprocess.run(['make', 'clean', 'all', 'CROSS=', 'CC='+compiler], cwd=tree, check=True)
        binary = tree / 'dist/cc2-control/cc2-control'
        web = tree / 'dist/cc2-control/web/index.html'
        validate_arm_elf(binary)
        (OUTPUT / 'web').mkdir(parents=True)
        (OUTPUT / 'defaults').mkdir(parents=True)
        shutil.copy2(binary, OUTPUT / 'cc2-control')
        shutil.copy2(web, OUTPUT / 'web/index.html')
        # Older snapshots embed their translations in index.html; newer ones
        # fetch /i18n/<code>.json and must ship web/locales alongside it.
        locales = tree / 'dist/cc2-control/web/locales'
        if '/i18n/' in web.read_text(encoding='utf-8') and not (locales / 'en.json').is_file():
            raise RuntimeError('CC2 Control UI loads /i18n/ but dist has no web/locales/en.json')
        if locales.is_dir():
            (OUTPUT / 'web/locales').mkdir()
            for locale in sorted(locales.glob('*.json')):
                shutil.copy2(locale, OUTPUT / 'web/locales' / locale.name)
                (OUTPUT / 'web/locales' / locale.name).chmod(0o644)
        shutil.copy2(tree / 'firmware-integration/overlay/opt/inst/cc2-control/defaults/material-presets.json',
                     OUTPUT / 'defaults/material-presets.json')
    for name in ('start.sh', 'launch.sh', 'cc2-control.init', 'cc2-configure'):
        copy_shell_lf(RUNTIME / name, OUTPUT / name)
    for name in ('cc2-control', 'start.sh', 'launch.sh', 'cc2-configure'):
        (OUTPUT / name).chmod(0o755)
    (OUTPUT / 'cc2-control.init').chmod(0o755)
    (OUTPUT / 'web/index.html').chmod(0o644)
    (OUTPUT / 'defaults/material-presets.json').chmod(0o644)
    files = {}
    for path in sorted(p for p in OUTPUT.rglob('*') if p.is_file()):
        files[path.relative_to(OUTPUT).as_posix()] = {
            'sha256': sha256(path),
            'mode': oct(prepared_mode(path.relative_to(OUTPUT).as_posix())),
        }
    manifest = {
        'component': 'CC2 Control',
        'version': VERSION,
        'source_sha256': SOURCE_SHA256,
        'source_commit': SOURCE_COMMIT,
        'files': files,
    }
    MANIFEST.write_text(json.dumps(manifest, indent=2, sort_keys=True) + '\n', encoding='utf-8')
    print(f'CC2 Control {VERSION} prepared:', OUTPUT)
    print('Manifest:', MANIFEST)

if __name__ == '__main__':
    main()

