"""Package an audited scratch .13 RootFS for a private Dual Trust trial."""

import hashlib
import io
import json
import os
from pathlib import Path
import shutil
import stat
import subprocess
import tempfile
import zipfile

import firmware_builder as fw
from resource_update import patch as patch_resources

STOCK_SWU_SHA = 'dcff98bc2e6513c6ac0ba5c8e08c6a582bef88226f915421566097ec46b6c6db'


def inventory(root):
    result = {}
    for path in root.rglob('*'):
        meta = path.lstat()
        value = None
        if stat.S_ISLNK(meta.st_mode):
            value = os.readlink(path)
        elif stat.S_ISREG(meta.st_mode):
            value = fw.sha256(path)
        elif stat.S_ISCHR(meta.st_mode) or stat.S_ISBLK(meta.st_mode):
            value = meta.st_rdev
        result[path.relative_to(root).as_posix()] = (meta.st_mode, value)
    return result


def verify_envelope(package, work, swu, inner_name, aes, public):
    outer = fw.validate_eleg(package, 0x04, encrypted=False)
    fw.verify_signature(package, public)
    if outer['data_size'] != len(outer['payload']):
        raise RuntimeError('Unexpected outer payload length')
    with zipfile.ZipFile(io.BytesIO(outer['payload'])) as archive:
        names = ['ota-package-list.json.sig', inner_name]
        if archive.namelist() != names or any(i.compress_type != zipfile.ZIP_STORED for i in archive.infolist()):
            raise RuntimeError('Unexpected signed ZIP members')
        decoded = []
        for name, kind in zip(names, (0x83, 0x80)):
            path = work / ('verify-' + name)
            path.write_bytes(archive.read(name))
            info = fw.validate_eleg(path, kind, encrypted=True)
            fw.verify_signature(path, public)
            plain = fw.pkcs7_unpad(fw.aes_cbc(info['payload'], aes, info['iv'], decrypt=True))
            if len(plain) != info['data_size']:
                raise RuntimeError('Unexpected decrypted length: ' + name)
            decoded.append(plain)
        expected = {'packages': [{'file': inner_name, 'hash': fw.sha256(work / ('verify-' + inner_name))}],
                    'version': '02.01.01.13', 'update_class': '00.00.00.00'}
        if json.loads(decoded[0]) != expected or hashlib.sha256(decoded[1]).hexdigest() != fw.sha256(swu):
            raise RuntimeError('Signed manifest or SWU differs from the audited inputs')


def package_trial(root, stock_swu, work, output, private, aes, public, mk, un, *, signing_mode='community'):
    if public != fw.signing_public(signing_mode):
        raise RuntimeError('Verification key does not match the selected signing mode')
    fw.reqhash(stock_swu, STOCK_SWU_SHA, '.13 verified decrypted stock SWU')
    cpio = work / 'cpio'
    cpio.mkdir()
    with stock_swu.open('rb') as data:
        names = subprocess.check_output(['cpio', '--quiet', '-it'], stdin=data, text=True).splitlines()
    if names != list(fw.CPIO_FILES):
        raise RuntimeError('Unexpected .13 stock CPIO member list')
    with stock_swu.open('rb') as data:
        subprocess.run(['cpio', '--quiet', '-idmu', '--no-absolute-filenames'],
                       stdin=data, cwd=cpio, check=True, timeout=120)
    unchanged = {name: fw.sha256(cpio / name) for name in ('resource', 'uboot', 'boot0', 'kernel')}
    desc = cpio / 'sw-description'
    desc.write_bytes(patch_resources(desc.read_bytes()))
    console = root / 'dev/console'
    if not stat.S_ISCHR(console.lstat().st_mode):
        raise RuntimeError('Full image requires the original console device node')
    before = inventory(root)
    image = work / 'rootfs.new'
    subprocess.run([mk, root, image, '-comp', 'xz', '-b', '262144', '-noappend',
                    '-no-tailends', '-exports', '-all-root', '-processors', '2'],
                   check=True, timeout=900)
    extracted = work / 'verified-rootfs'
    subprocess.run([un, '-processors', '2', '-d', extracted, image], check=True, timeout=240)
    after = inventory(extracted)
    if before != after:
        differences = sorted(name for name in before.keys() | after.keys() if before.get(name) != after.get(name))
        raise RuntimeError('RootFS round-trip differs: ' + ', '.join(differences[:8]))
    print('PASS: full RootFS round-trip, file contents, modes, symlinks and device nodes')
    shutil.copyfile(image, cpio / 'rootfs')
    checksums = ''.join(f'{fw.md5(cpio / name)}  {name}\n' for name in fw.MD5_FILES)
    (cpio / 'cpio_item_md5').write_text(checksums, encoding='ascii', newline='\n')
    swu = work / 'trial.swu'
    members = ('\n'.join(fw.CPIO_FILES) + '\n').encode()
    with swu.open('wb') as out:
        subprocess.run(['cpio', '--quiet', '-o', '-H', 'crc', '--owner=1004:1002'],
                       input=members, stdout=out, cwd=cpio, check=True, timeout=180)
    verified = work / 'verified-cpio'
    verified.mkdir()
    with swu.open('rb') as data:
        subprocess.run(['cpio', '--quiet', '-idmu', '--no-absolute-filenames'],
                       stdin=data, cwd=verified, check=True, timeout=180)
    for name in fw.CPIO_FILES:
        fw.reqhash(verified / name, fw.sha256(cpio / name), 're-extracted SWU ' + name)
    for name, digest in unchanged.items():
        fw.reqhash(verified / name, digest, 'unchanged stock SWU ' + name)
    stem = 'CC2_02.01.01.13_PRIVATE_TRIAL' if signing_mode == 'community' else 'CC2_02.01.01.13_STOCK_BOOTSTRAP'
    inner_name = stem + '.swu.sig'
    inner = work / inner_name
    fw.eleg_encrypted(swu.read_bytes(), inner, aes, private, inner_name[:-4], 0x80)
    manifest = work / 'ota-package-list.json.sig'
    data = {'packages': [{'file': inner_name, 'hash': fw.sha256(inner)}],
            'version': '02.01.01.13', 'update_class': '00.00.00.00'}
    fw.eleg_encrypted(json.dumps(data, separators=(',', ':')).encode(), manifest, aes,
                      private, 'ota-package-list.json', 0x83)
    bundle = work / (stem + '.zip')
    with zipfile.ZipFile(bundle, 'w', compression=zipfile.ZIP_STORED) as archive:
        archive.write(manifest, manifest.name)
        archive.write(inner, inner.name)
    signed = work / (bundle.name + '.sig')
    fw.eleg_plain(bundle.read_bytes(), signed, private, bundle.name)
    verify_envelope(signed, work, swu, inner_name, aes, public)
    with tempfile.NamedTemporaryFile(prefix='.cc2-trial-', dir=output.parent, delete=False) as temp:
        path = Path(temp.name)
        try:
            with signed.open('rb') as data:
                shutil.copyfileobj(data, temp)
            temp.flush()
            os.fsync(temp.fileno())
            os.link(path, output)
        finally:
            path.unlink()
    print('PASS: verified private .13 package', output, fw.sha256(output))
