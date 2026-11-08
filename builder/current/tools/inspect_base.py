#!/usr/bin/env python3
"""Offline inspector for a new official ELEGOO CC2 base package.

Reads a user-supplied official .zip.sig, reports SHA-256 values for every
container stage, CPIO member and the vendor executables patched by the
builder, and checks whether each qualified 02.01.01.07 patch site still holds
its original bytes. It never modifies the package, never contacts the network
and does not build or sign firmware. Nothing here may be used to bypass the
printer's version or signature checks.

Run with an isolated interpreter, for example:
  python3 -I tools/inspect_base.py original_firmware/<package>.zip.sig \
      --aes-key keys/cc2_aes_key_v1.bin --out analysis
Outputs (report.json and extracted vendor members) stay in the ignored
analysis/ directory and must never be committed.
"""
import argparse
import hashlib
import importlib.util
import io
import json
import re
import shutil
import subprocess
import sys
import zipfile
from pathlib import Path

BASE = Path(__file__).resolve().parents[1]
REPO = BASE.parents[1]
PORT = REPO / 'experimental/firmware-port'
ROOTFS_FILES = ('opt/bin/ec-eeb001-gui', 'opt/bin/elegoo_printer', 'opt/inst/daemon-000/daemon-000',
                'opt/bin/ai_camera', 'opt/lib/libcolib.so', 'etc/init.d/printer', 'opt/bin/run_printer.sh',
                'opt/inst/firmware_version/versions.json', 'opt/inst/printer_dsp.cfg')
PIN_FOR_ROOTFS = {'opt/bin/ec-eeb001-gui': 'stock_gui', 'opt/bin/elegoo_printer': 'stock_printer',
                  'opt/inst/daemon-000/daemon-000': 'daemon_stock', 'opt/bin/ai_camera': 'stock_ai_camera',
                  'opt/lib/libcolib.so': 'libcolib'}
PIN_FOR_CPIO = {'sw-description': 'sw-description', 'resource': 'resource', 'uboot': 'uboot',
                'boot0': 'boot0', 'kernel': 'kernel', 'rootfs': 'stock_rootfs'}
SAFE_NAME = re.compile(r'[A-Za-z0-9._-]{1,128}')


def load_builder():
    spec = importlib.util.spec_from_file_location('inspect_builder', BASE / 'core/firmware_builder.py')
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def digest(data):
    return hashlib.sha256(data).hexdigest()


def eleg_summary(builder, path):
    x = builder.eleg_info(path)
    return x, {'type': hex(x['type']), 'version': x['version'], 'subtype': x['subtype'],
               'embedded_filename': x['filename'], 'data_size': x['data_size'],
               'payload_size': x['payload_size'], 'payload_digest_ok': digest(x['payload']) == x['sha'].hex()}


def try_signature(builder, path):
    try:
        with io.StringIO() as sink:
            stdout, sys.stdout = sys.stdout, sink
            try:
                builder.verify_signature(path, builder.STOCK_PUBLIC)
            finally:
                sys.stdout = stdout
        return 'valid with keys/cc2_stock_public.pem'
    except Exception as error:  # report, never abort: a rotated key is a finding
        return 'NOT valid with keys/cc2_stock_public.pem (' + str(error).splitlines()[0] + ')'


def decrypt(builder, info, aes):
    plain = builder.pkcs7_unpad(builder.aes_cbc(info['payload'], aes, info['iv'], decrypt=True))
    if len(plain) != info['data_size']:
        raise RuntimeError('decrypted length does not match ELEG data_size')
    return plain


def parse_newc(data):
    """Yield (name, mode, content) from a newc/crc CPIO archive without running cpio."""
    pos = 0
    while True:
        header = data[pos:pos + 110]
        if len(header) < 110 or header[:6] not in (b'070701', b'070702'):
            raise RuntimeError(f'CPIO header expected at {pos:#x}')
        fields = [int(header[6 + 8 * i:14 + 8 * i], 16) for i in range(13)]
        mode, size, namesize = fields[1], fields[6], fields[11]
        name_start = pos + 110
        name = data[name_start:name_start + namesize - 1].decode('utf-8', 'replace')
        pos = (name_start + namesize + 3) & ~3
        if name == 'TRAILER!!!':
            return
        content = data[pos:pos + size]
        pos = (pos + size + 3) & ~3
        yield name, mode, content


def zoffset_site():
    text = (PORT / 'patch_zoffset44.py').read_text(encoding='utf-8')
    offset = int(re.search(r'^OFFSET=(0x[0-9a-fA-F]+)', text, re.M).group(1), 16)
    before = re.search(r"^ORIGINAL=bytes\.fromhex\('([0-9a-f]+)'\)", text, re.M).group(1)
    return [{'manifest': 'patch_zoffset44.py', 'target': 'opt/bin/ec-eeb001-gui', 'offset': offset,
             'va': None, 'before': before, 'note': ''}]


def manifest_sites(name, target, note=''):
    manifest = json.loads((PORT / name).read_text(encoding='utf-8'))
    return [{'manifest': name, 'target': target, 'offset': int(p['offset']), 'va': p.get('va'),
             'before': p['before'], 'note': note} for p in manifest.get('patches', [])]


def patch_sites():
    sites = zoffset_site()
    sites += manifest_sites('camera44-manifest.json', 'opt/bin/ai_camera')
    sites += manifest_sites('http44-manifest.json', 'opt/bin/elegoo_printer')
    sites += manifest_sites('network44-manifest.json', 'opt/bin/elegoo_printer',
                            'applies to the HTTP-patched intermediate; bytes may differ from stock by design')
    dual = json.loads((PORT / 'dual44-manifest.json').read_text(encoding='utf-8'))
    sites.append({'manifest': 'dual44-manifest.json', 'target': 'opt/inst/daemon-000/daemon-000',
                  'offset': int(dual['hook_file_offset']), 'va': hex(int(dual['hook_va'])),
                  'before': dual['hook_original'], 'note': 'Dual Trust hook'})
    return sites


def check_site(site, data):
    before = bytes.fromhex(site['before'])
    at_offset = data[site['offset']:site['offset'] + len(before)] == before
    hits, start = [], 0
    while len(hits) < 4:
        found = data.find(before, start)
        if found < 0:
            break
        hits.append(found)
        start = found + 1
    if at_offset and len(hits) == 1:
        status = 'unchanged at qualified offset'
    elif at_offset:
        status = 'at qualified offset, but pattern is not unique'
    elif len(hits) == 1:
        status = f'moved: unique match at {hits[0]:#x}'
    elif hits:
        status = f'ambiguous: {len(hits)}{"+" if len(hits) == 4 else ""} matches elsewhere'
    else:
        status = 'not found'
    return dict(site, offset=hex(site['offset']), before_len=len(before), status=status,
                matches=[hex(h) for h in hits])


def find_unsquashfs(builder, explicit):
    if explicit:
        return Path(explicit)
    for candidate in (BASE / 'tools/squashfs-tools-4.6.1/bin/unsquashfs',):
        if candidate.is_file():
            return candidate
    found = shutil.which('unsquashfs')
    return Path(found) if found else None


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument('package', help='official ELEGOO .zip.sig supplied by the user')
    ap.add_argument('--aes-key', help='local AES input (keys/cc2_aes_key_v1.bin); without it only the outer layer is inspected')
    ap.add_argument('--out', default=str(BASE / 'analysis'), help='new or empty output directory (ignored by Git)')
    ap.add_argument('--unsquashfs', help='unsquashfs binary used to extract only the inspected RootFS files')
    ap.add_argument('--rootfs-dir', help='already extracted RootFS to inspect instead of running unsquashfs')
    a = ap.parse_args(argv)

    builder = load_builder()
    package = Path(a.package).resolve()
    out = Path(a.out).resolve()
    if out.exists() and any(out.iterdir()):
        raise SystemExit(f'Output directory is not empty: {out}')
    out.mkdir(parents=True, exist_ok=True)
    raw = package.read_bytes()
    report = {'tool': 'inspect_base.py', 'package_name': package.name,
              'qualified_base': builder.QUALIFIED_BASE, 'findings': []}

    outer, report['outer'] = eleg_summary(builder, package)
    report['outer'].update(size=len(raw), sha256=digest(raw), signature=try_signature(builder, package))
    if outer['type'] != 0x04 or outer['version'] != 1 or outer['subtype'] != 2:
        report['findings'].append('Outer ELEG identity differs from the qualified format (0x04/v1/subtype 2).')
    with zipfile.ZipFile(io.BytesIO(outer['payload'])) as z:
        report['zip'] = [{'name': i.filename, 'size': i.file_size, 'compress_type': i.compress_type} for i in z.infolist()]
        members = {i.filename: z.read(i.filename) for i in z.infolist()
                   if not i.is_dir() and SAFE_NAME.fullmatch(i.filename)}
    if len(members) != len([m for m in report['zip'] if not m['name'].endswith('/')]):
        report['findings'].append('ZIP contains member names outside the expected flat pattern; they were not read.')
    swusigs = [n for n in members if n.lower().endswith('.swu.sig')]
    if len(swusigs) != 1 or 'ota-package-list.json.sig' not in members:
        report['findings'].append(f'Expected one .swu.sig and ota-package-list.json.sig, found: {sorted(members)}')
    for name in members:
        (out / name).write_bytes(members[name])

    aes = Path(a.aes_key).read_bytes() if a.aes_key else None
    if aes is not None and digest(aes) != builder.EXPECTED['aes_key']:
        report['findings'].append('AES input differs from the qualified AES key v1.')
    if 'ota-package-list.json.sig' in members:
        info, report['manifest_eleg'] = eleg_summary(builder, out / 'ota-package-list.json.sig')
        report['manifest_eleg']['signature'] = try_signature(builder, out / 'ota-package-list.json.sig')
        if aes is not None:
            try:
                report['manifest'] = json.loads(decrypt(builder, info, aes))
            except Exception as error:
                report['findings'].append('Manifest could not be decrypted with the AES input: ' + str(error).splitlines()[0])
    if len(swusigs) == 1:
        inner_path = out / swusigs[0]
        info, report['inner'] = eleg_summary(builder, inner_path)
        report['inner'].update(name=swusigs[0], sha256=digest(inner_path.read_bytes()),
                               signature=try_signature(builder, inner_path))
        swu = None
        if aes is not None:
            try:
                swu = decrypt(builder, info, aes)
            except Exception as error:
                report['findings'].append('SWU could not be decrypted with the AES input: ' + str(error).splitlines()[0])
        if swu is not None:
            report['swu'] = {'size': len(swu), 'sha256': digest(swu)}
            cpio_dir = out / 'cpio'
            cpio_dir.mkdir()
            report['cpio'] = {}
            for name, mode, content in parse_newc(swu):
                report['cpio'][name] = {'sha256': digest(content), 'size': len(content), 'mode': oct(mode & 0o7777)}
                if SAFE_NAME.fullmatch(name):
                    (cpio_dir / name).write_bytes(content)
                else:
                    report['findings'].append(f'CPIO member with unexpected name not extracted: {name!r}')
            expected = list(builder.CPIO_FILES)
            if list(report['cpio']) != expected:
                report['findings'].append(f'CPIO members differ from the qualified order {expected}: {list(report["cpio"])}')
            if 'sw-description' in report['cpio']:
                report['sw_description'] = (cpio_dir / 'sw-description').read_text('utf-8', 'replace')

    rootfs_dir = Path(a.rootfs_dir).resolve() if a.rootfs_dir else None
    rootfs_image = out / 'cpio' / 'rootfs'
    if rootfs_dir is None and rootfs_image.is_file():
        unsquashfs = find_unsquashfs(builder, a.unsquashfs)
        if unsquashfs is None:
            report['findings'].append('unsquashfs not available; RootFS files were not inspected.')
        else:
            rootfs_dir = out / 'rootfs'
            subprocess.run([str(unsquashfs), '-no-progress', '-d', str(rootfs_dir), str(rootfs_image), *ROOTFS_FILES],
                           check=False, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    if rootfs_dir is not None:
        report['rootfs_files'] = {}
        for relative in ROOTFS_FILES:
            path = rootfs_dir / relative
            report['rootfs_files'][relative] = digest(path.read_bytes()) if path.is_file() and not path.is_symlink() else None
        report['patch_sites'] = []
        for site in patch_sites():
            path = rootfs_dir / site['target']
            if path.is_file():
                report['patch_sites'].append(check_site(site, path.read_bytes()))
            else:
                report['patch_sites'].append(dict(site, offset=hex(site['offset']), status='target file missing'))

    qualified = builder.QUALIFIED_PROFILE['sha256']
    values = {'stock_outer': report['outer']['sha256'], 'stock_inner': report.get('inner', {}).get('sha256'),
              'stock_swu': report.get('swu', {}).get('sha256')}
    values.update({pin: report.get('cpio', {}).get(member, {}).get('sha256') for member, pin in PIN_FOR_CPIO.items()})
    values.update({pin: report.get('rootfs_files', {}).get(path) for path, pin in PIN_FOR_ROOTFS.items()})
    report['profile_values'] = {
        'stock_package': package.name,
        'embedded_filename (choose the version substring as embedded_filename_marker)': report['outer']['embedded_filename'],
        'manifest': {k: report.get('manifest', {}).get(k) for k in ('version', 'update_class')},
        'sha256': values,
        'same_as_qualified_02.01.01.07': sorted(k for k, v in values.items() if v and v == qualified.get(k)),
    }
    (out / 'report.json').write_text(json.dumps(report, indent=1) + '\n', encoding='utf-8')

    print('Report      :', out / 'report.json')
    print('Outer SHA256:', report['outer']['sha256'])
    print('Outer ELEG  :', report['outer']['type'], 'v%s' % report['outer']['version'], report['outer']['embedded_filename'])
    print('Signature   :', report['outer']['signature'])
    if 'manifest' in report:
        print('Manifest    :', {k: report['manifest'].get(k) for k in ('version', 'update_class')})
    for site in report.get('patch_sites', []):
        print(f"Patch site  : {site['manifest']} {site['offset']} -> {site['status']}")
    for finding in report['findings']:
        print('FINDING     :', finding)
    missing = [k for k, v in values.items() if not v]
    if missing:
        print('Not determined:', ', '.join(missing))
    print('Vendor content was written only to', out, '- never commit it.')
    return 0


if __name__ == '__main__':
    sys.exit(main())
