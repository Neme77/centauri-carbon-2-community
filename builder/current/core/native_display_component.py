"""Install the reviewed native presentation into the firmware's own resources.

The vendor init remains responsible for synchronizing /opt/inst/images before
starting the GUI. This component never installs a GUI launcher or touches UDISK.
"""
from pathlib import Path, PurePosixPath
import hashlib
import io
import importlib.util
import json
import re
import subprocess
import struct
import sys
import tarfile
import tempfile
import zlib

SOURCE = Path(__file__).resolve().parents[3] / 'experimental/native-display'
GUI_SHA256 = 'db09fa37d378df259427aaac392a341282e08ceb26cd87603542c0dcbcbb91d1'
# Verified against the exact official RootFS. These illustrations are NOT
# replaced by the presentation package; installation and final audits require
# their continued presence and exact identity.
PRESERVED = {
    'images/home_menu_illustration_printer_light_off.png':
        '20964211688a113d7c539ad1c3fd240d7e32c7393aae59bbb0ed15fd10d7af57',
    'images/home_menu_illustration_printer_light_on.png':
        '32d03b7b46acc68f5e0e0dedfb5930a8f091d44cc185c4036477d89f9e076ffb',
}


def output_matches(name, data, expected):
    # The public source uses a lossless standard-library encoder for selected
    # vendor glyphs. The old private preview used different PNG compression;
    # its byte hashes are NOT the output identity for these fifty files.
    # Require the reviewed dimensions and every RGBA byte instead.
    recolored = { 'images/' + item['name']: item for item in json.loads(
        (SOURCE / 'recolor-plan.json').read_text(encoding='utf-8'))['assets'] }
    if name not in recolored:
        return sha(data) == expected
    spec = importlib.util.spec_from_file_location('firmware_png_palette', SOURCE / 'png_palette.py')
    palette = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(palette)
    try:
        width, height, rgba = palette.decode(data)
    except (AssertionError, ValueError, IndexError, struct.error, zlib.error):
        return False
    plan = recolored[name]
    return ((width, height) == (plan['width'], plan['height'])
            and sha(rgba) == plan['expected_rgba_sha256'])


def sha(data):
    return hashlib.sha256(data).hexdigest()


def pins():
    result = {}
    for line in (SOURCE / 'baseline-pins.txt').read_text(encoding='utf-8').splitlines():
        before, after, name = line.split()
        path = PurePosixPath(name)
        if (not re.fullmatch('[a-f0-9]{64}', before)
                or not re.fullmatch('[a-f0-9]{64}', after)
                or name in result or len(path.parts) != 2
                or str(path) != name or path.name in ('.', '..')
                or not (name == 'gui/ec-eeb001-gui'
                        or (path.parts[0] == 'images' and path.suffix == '.png'))):
            raise RuntimeError('Invalid native display resource pin')
        result[name] = (before, after)
    if result.get('gui/ec-eeb001-gui', ('', ''))[1] != GUI_SHA256:
        raise RuntimeError('Native display output identity changed')
    for name, digest in PRESERVED.items():
        if name in result:
            raise RuntimeError('Original printer artwork must not be replaced')
        result[name] = (digest, digest)
    return result


def target(root, name):
    relative = ('opt/bin/ec-eeb001-gui' if name == 'gui/ec-eeb001-gui'
                else 'opt/inst/images/theme_default/' + PurePosixPath(name).name)
    path = root / relative
    for parent in (path, *path.parents):
        if parent == root:
            break
        if parent.is_symlink():
            raise RuntimeError('Symlink in native display firmware target')
    if not path.is_file():
        raise RuntimeError('Missing native display firmware resource: ' + name)
    return path


def audit(root):
    root = Path(root)
    for name, (_before, after) in pins().items():
        if not output_matches(name, target(root, name).read_bytes(), after):
            raise RuntimeError('Native display firmware audit failed: ' + name)


def install(root):
    root = Path(root)
    expected = pins()
    # Validate the complete source set before changing any extracted file.
    for name, (before, _after) in expected.items():
        if sha(target(root, name).read_bytes()) != before:
            raise RuntimeError('Unsupported native display firmware baseline: ' + name)
    with tempfile.TemporaryDirectory(prefix='cc2-native-display-') as temporary:
        output = Path(temporary) / 'component.tar.gz'
        subprocess.run([sys.executable, str(SOURCE / 'package.py'), '--native',
                        str(root / 'opt/bin/ec-eeb001-gui'), '--stock-images',
                        str(root / 'opt/inst/images/theme_default'), '--baseline',
                        'clean', '--output', str(output)], check=True, timeout=120)
        changes = {}
        with tarfile.open(fileobj=io.BytesIO(output.read_bytes()), mode='r:gz') as archive:
            seen = set()
            for entry in archive:
                if not entry.isfile() or entry.name in seen:
                    raise RuntimeError('Invalid generated native display archive')
                seen.add(entry.name)
                if entry.name not in expected:
                    continue
                if entry.size > 4 * 1024 * 1024:
                    raise RuntimeError('Oversized native display firmware resource')
                data = archive.extractfile(entry).read()
                if not output_matches(entry.name, data, expected[entry.name][1]):
                    raise RuntimeError('Generated native display identity differs: ' + entry.name)
                changes[entry.name] = data
        if set(changes) != set(expected) - set(PRESERVED):
            raise RuntimeError('Incomplete generated native display component')
        for name, data in changes.items():
            path = target(root, name)
            path.write_bytes(data)
            path.chmod(0o755 if name.startswith('gui/') else 0o644)
    audit(root)
