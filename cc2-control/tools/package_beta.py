#!/usr/bin/env python3
"""Package an already built ARM distribution; never uploads to the printer."""
import argparse
import hashlib
import json
from pathlib import Path
import shutil
import subprocess
import tarfile
import zipfile

ROOT = Path(__file__).resolve().parents[2]
CC2 = ROOT / 'cc2-control'
PRS = {
    46: 'fix/gcode-upload-128-mib',
    51: 'fix/ui-audit-regressions',
    52: 'fix/http-browser-request-guard',
    53: 'fix/calibration-measurement-session',
    54: 'fix/z-offset-readback',
    55: 'fix/exclude-object-unicode-names',
}


def git(*args):
    return subprocess.check_output(['git', '-C', str(ROOT), *args], text=True).strip()


def checksum(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def sums(directory):
    files = sorted(p for p in directory.rglob('*') if p.is_file() and p.name != 'SHA256SUMS')
    (directory / 'SHA256SUMS').write_text(''.join(
        f'{checksum(p)}  {p.relative_to(directory).as_posix()}\n' for p in files
    ))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--source-commit', default=None)
    parser.add_argument('--compiler', default='arm-linux-gnueabihf-gcc')
    args = parser.parse_args()
    source = args.source_commit or git('rev-parse', 'HEAD')
    # Packaging is allowed only from the exact source recorded in the manifest.
    if git('rev-parse', 'HEAD') != source or git('status', '--porcelain'):
        raise SystemExit('Commit sources and match --source-commit before packaging.')
    build_id = source[:8]
    name = f'CC2-Control-Beta-{build_id}-AllPR-Multiplatform-Update'
    output = args.output.resolve()
    output.mkdir(parents=True, exist_ok=True)
    folder, payload = output / name, output / 'payload'
    if folder.exists() or payload.exists():
        raise SystemExit('Output already contains a package; use a new directory.')
    dist = CC2 / 'dist/cc2-control'
    binary = dist / 'cc2-control'
    elf = binary.read_bytes()
    if elf[:5] != b'\x7fELF\x01' or int.from_bytes(elf[18:20], 'little') != 40:
        raise SystemExit('Expected a 32-bit ARM ELF printer build, not a host binary.')
    shutil.copytree(dist, payload)
    folder.mkdir()
    templates = CC2 / 'installer/beta'
    binary_hash = checksum(binary)
    for filename in ['install-on-printer.sh', 'restore-on-printer.sh']:
        body = (templates / filename).read_text().replace('@BINARY_SHA256@', binary_hash)
        (payload / filename).write_text(body)
        (payload / filename).chmod(0o755)
    manifest = {
        'build': name,
        'runtime_version': '1.1.31',
        'repository': 'https://github.com/Neme77/centauri-carbon-2-community',
        'source_commit': source,
        'develop_commit': git('merge-base', 'HEAD', 'origin/develop'),
        'pull_requests': {str(pr): git('rev-parse', f'origin/{branch}') for pr, branch in PRS.items()},
        'binary_sha256': binary_hash,
        'compiler': subprocess.check_output([args.compiler, '--version'], text=True).splitlines()[0],
        'hardware_validation': 'pending: combined build must be tested on the printer',
    }
    body = json.dumps(manifest, indent=2) + '\n'
    (payload / 'beta-build.json').write_text(body)
    (folder / 'beta-build.json').write_text(body)
    sums(payload)
    archive = folder / f'cc2-control-beta-{build_id}-payload.tar.gz'
    with tarfile.open(archive, 'w:gz') as tar:
        for item in sorted(payload.iterdir()):
            tar.add(item, arcname=item.name)
    for filename in ['Install-CC2-Control.ps1', 'install-cc2-control.sh', 'Install-Windows.cmd']:
        body = (templates / filename).read_text().replace('@BUILD_ID@', build_id)
        (folder / filename).write_text(body)
    (folder / 'README.md').write_text((templates / 'README.md').read_text().replace('@BUILD_ID@', build_id))
    sums(folder)
    path = output / f'{name}.zip'
    with zipfile.ZipFile(path, 'w', compression=zipfile.ZIP_DEFLATED) as zipout:
        for item in sorted(folder.rglob('*')):
            if item.is_file():
                zipout.write(item, item.relative_to(output))
    print(path)
    print(f'Binary SHA256: {binary_hash}')


if __name__ == '__main__':
    main()
