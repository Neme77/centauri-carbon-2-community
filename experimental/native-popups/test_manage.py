# SPDX-License-Identifier: GPL-3.0-or-later
"""manage.sh against a temporary root and a fake CC2 Control /api/printer.

Runs with sh (dash on Debian/Ubuntu) and, when installed, BusyBox sh.
"""
import hashlib
import http.server
import json
import os
import shutil
import subprocess
import tempfile
import threading
import unittest

HERE = os.path.dirname(os.path.abspath(__file__))
STOCK, PATCHED = b'stock screen program\n', b'patched screen program\n'
sha = lambda b: hashlib.sha256(b).hexdigest()


class Printer(http.server.BaseHTTPRequestHandler):
    status = 'Idle'

    def do_GET(self):
        body = json.dumps({'machine': {'status_name': Printer.status}}).encode()
        self.send_response(200)
        self.send_header('Content-Length', str(len(body)))
        self.end_headers()
        self.wfile.write(body)

    def log_message(self, *args):
        pass


def shells():
    found = [['sh']]
    if shutil.which('busybox'):
        found.append(['busybox', 'sh'])
    return found


class ManageTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.server = http.server.ThreadingHTTPServer(('127.0.0.1', 0), Printer)
        threading.Thread(target=cls.server.serve_forever, daemon=True).start()

    @classmethod
    def tearDownClass(cls):
        cls.server.shutdown()

    def setUp(self):
        Printer.status = 'Idle'
        self.dir = tempfile.mkdtemp()
        self.root = os.path.join(self.dir, 'root')
        os.makedirs(os.path.join(self.root, 'opt/bin'))
        self.gui = os.path.join(self.root, 'opt/bin/ec-eeb001-gui')
        self.write(self.gui, STOCK)
        self.build = os.path.join(self.dir, 'build')
        os.makedirs(self.build)
        self.write(os.path.join(self.build, 'ec-eeb001-gui'), PATCHED)
        self.manifest({'input_sha256': sha(STOCK), 'output_sha256': sha(PATCHED)})

    def tearDown(self):
        shutil.rmtree(self.dir)

    def write(self, path, data):
        with open(path, 'wb') as f:
            f.write(data)

    def read(self, path):
        with open(path, 'rb') as f:
            return f.read()

    def manifest(self, m):
        self.write(os.path.join(self.build, 'manifest.json'), json.dumps(dict(schema=1, **m), indent=2).encode())

    def run_manage(self, shell, *args):
        env = dict(os.environ, NUI_TEST_ROOT=self.root, NUI_TEST_STOCK_SHA=sha(STOCK),
                   NUI_TEST_PRINTER_API=f'http://127.0.0.1:{self.server.server_port}/api/printer')
        r = subprocess.run([*shell, os.path.join(HERE, 'manage.sh'), *args], env=env, capture_output=True, text=True, timeout=30)
        return r.returncode, r.stdout + r.stderr

    def test_install_and_remove(self):
        for shell in shells():
            with self.subTest(shell=shell[0]):
                self.write(self.gui, STOCK)
                self.assertIn('stock', self.run_manage(shell, 'status')[1])
                code, out = self.run_manage(shell, 'install', self.build)
                self.assertEqual(code, 0, out)
                self.assertEqual(self.read(self.gui), PATCHED)
                self.assertEqual(os.stat(self.gui).st_mode & 0o777, 0o755)
                self.assertEqual(self.read(os.path.join(self.root, 'opt/usr/cc2-native-popups/ec-eeb001-gui.stock')), STOCK)
                self.assertIn('installed', self.run_manage(shell, 'status')[1])
                self.assertIn('Already installed', self.run_manage(shell, 'install', self.build)[1])
                code, out = self.run_manage(shell, 'remove')
                self.assertEqual(code, 0, out)
                self.assertEqual(self.read(self.gui), STOCK)
                self.assertIn('Already the stock', self.run_manage(shell, 'remove')[1])
                self.assertEqual(sorted(os.listdir(os.path.join(self.root, 'opt/usr/cc2-native-popups'))), ['ec-eeb001-gui.stock'])

    def test_refusals_change_nothing(self):
        for shell in shells():
            with self.subTest(shell=shell[0]):
                self.write(self.gui, STOCK)
                Printer.status = 'Printing'
                code, out = self.run_manage(shell, 'install', self.build)
                self.assertNotEqual(code, 0)
                self.assertIn('idle', out)
                Printer.status = 'Idle'
                self.manifest({'input_sha256': sha(STOCK), 'output_sha256': sha(b'other')})
                self.assertIn('does not match', self.run_manage(shell, 'install', self.build)[1])
                self.manifest({'input_sha256': sha(b'another program'), 'output_sha256': sha(PATCHED)})
                self.assertIn('another screen program', self.run_manage(shell, 'install', self.build)[1])
                self.manifest({'input_sha256': sha(STOCK), 'output_sha256': sha(PATCHED)})
                self.write(self.gui, b'someone else\n')
                self.assertIn('not the stock', self.run_manage(shell, 'install', self.build)[1])
                code, out = self.run_manage(shell, 'remove')
                self.assertNotEqual(code, 0)
                self.assertIn('not installed here', out)
                self.assertEqual(self.read(self.gui), b'someone else\n')
                self.assertNotEqual(self.run_manage(shell, 'bogus')[0], 0)


if __name__ == '__main__':
    unittest.main()
