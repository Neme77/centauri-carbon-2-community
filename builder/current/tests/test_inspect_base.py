"""The offline base inspector reports a new package without trusting or changing it.

Objective: run tools/inspect_base.py on a synthetic package that mimics the
qualified ELEG/ZIP/CPIO layout but is signed with a throw-away test key and a
random AES key. It must decode every layer, report the test signature as not
valid for the stock key, flag the foreign AES input, list CPIO hashes and
classify patch sites (unchanged, moved, not found) in an extracted RootFS.
No vendor content is used.
"""
from pathlib import Path
import contextlib
import importlib.util
import io
import json
import os
import shutil
import subprocess
import tempfile
import unittest
import zipfile

BASE = Path(__file__).resolve().parents[1]
REPO = BASE.parents[1]


def load(name, path):
    spec = importlib.util.spec_from_file_location(name, path)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def newc(entries):
    out = bytearray()
    for i, (name, data) in enumerate(entries + [('TRAILER!!!', b'')]):
        encoded = name.encode() + b'\0'
        fields = [i + 1, 0o100644 if data or name != 'TRAILER!!!' else 0, 0, 0, 1, 0, len(data), 0, 0, 0, 0, len(encoded), 0]
        out += b'070701' + b''.join(b'%08x' % f for f in fields) + encoded
        out += b'\0' * (-len(out) % 4) + data
        out += b'\0' * (-len(out) % 4)
    return bytes(out)


@unittest.skipUnless(shutil.which('openssl'), 'openssl required')
class InspectBaseTests(unittest.TestCase):
    def test_synthetic_package(self):
        b = load('inspect_test_builder', BASE / 'core/firmware_builder.py')
        tool = load('inspect_test_tool', BASE / 'tools/inspect_base.py')
        with tempfile.TemporaryDirectory(prefix='cc2-inspect-test-') as td:
            td = Path(td)
            key = td / 'test-only.pem'
            subprocess.run(['openssl', 'genrsa', '-out', str(key), '2048'], check=True, capture_output=True)
            aes = os.urandom(32)
            (td / 'aes.bin').write_bytes(aes)
            members = [(n, ('member ' + n).encode()) for n in b.CPIO_FILES]
            swu = newc(members)
            b.eleg_encrypted(swu, td / 'pkg.swu.sig', aes, key, 'pkg.swu', 0x80)
            manifest = json.dumps({'packages': [], 'version': 'TEST.VERSION', 'update_class': 'TEST.CLASS'}).encode()
            b.eleg_encrypted(manifest, td / 'ota-package-list.json.sig', aes, key, 'ota-package-list.json', 0x83)
            with zipfile.ZipFile(td / 'pkg.zip', 'w', compression=zipfile.ZIP_STORED) as z:
                z.write(td / 'ota-package-list.json.sig', 'ota-package-list.json.sig')
                z.write(td / 'pkg.swu.sig', 'pkg.swu.sig')
            package = td / 'synthetic_TEST.zip.sig'
            b.eleg_plain((td / 'pkg.zip').read_bytes(), package, key, 'synthetic_TEST.zip')

            sites = {s['manifest']: s for s in reversed(tool.patch_sites())}
            rootfs = td / 'rootfs'
            camera = sites['camera44-manifest.json']
            data = bytearray(camera['offset'] + 64)
            data[camera['offset']:camera['offset'] + len(bytes.fromhex(camera['before']))] = bytes.fromhex(camera['before'])
            (rootfs / 'opt/bin').mkdir(parents=True)
            (rootfs / 'opt/bin/ai_camera').write_bytes(bytes(data))
            http = sites['http44-manifest.json']
            (rootfs / 'opt/bin/elegoo_printer').write_bytes(b'\0' * 100 + bytes.fromhex(http['before']) + b'\0' * 100)

            out = td / 'analysis'
            with contextlib.redirect_stdout(io.StringIO()):
                self.assertEqual(tool.main([str(package), '--aes-key', str(td / 'aes.bin'), '--out', str(out),
                                            '--rootfs-dir', str(rootfs)]), 0)
            report = json.loads((out / 'report.json').read_text())
            self.assertEqual(report['outer']['embedded_filename'], 'synthetic_TEST.zip')
            self.assertTrue(report['outer']['signature'].startswith('NOT valid'))
            self.assertEqual(report['manifest']['version'], 'TEST.VERSION')
            self.assertEqual(list(report['cpio']), list(b.CPIO_FILES))
            self.assertEqual(report['cpio']['kernel']['sha256'], b.sha256(out / 'cpio/kernel'))
            self.assertTrue(any('AES input differs' in f for f in report['findings']))
            statuses = {(s['manifest'], s['offset']): s['status'] for s in report['patch_sites']}
            self.assertEqual(statuses[('camera44-manifest.json', hex(camera['offset']))], 'unchanged at qualified offset')
            self.assertEqual(statuses[('http44-manifest.json', hex(http['offset']))], 'moved: unique match at 0x64')
            self.assertEqual(statuses[('patch_zoffset44.py', hex(sites['patch_zoffset44.py']['offset']))], 'target file missing')
            values = report['profile_values']['sha256']
            self.assertEqual(values['stock_outer'], b.sha256(package))
            self.assertIsNone(values['daemon_stock'])
            with self.assertRaises(SystemExit):
                with contextlib.redirect_stdout(io.StringIO()):
                    tool.main([str(package), '--out', str(out)])


if __name__ == '__main__':
    unittest.main()
