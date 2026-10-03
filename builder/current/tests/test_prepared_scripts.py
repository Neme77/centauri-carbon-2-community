"""Prepared scripts must remain executable LF files on Windows checkouts."""
import importlib.util
from pathlib import Path
import tempfile
import unittest
spec = importlib.util.spec_from_file_location('prepare_scripts', Path(__file__).resolve().parents[1] / 'prepare.py')
prepare = importlib.util.module_from_spec(spec)
spec.loader.exec_module(prepare)
class PreparedScripts(unittest.TestCase):
    def test_normalizes_bom_crlf_and_cr(self):
        with tempfile.TemporaryDirectory() as temporary:
            source = Path(temporary) / 'source'; output = Path(temporary) / 'output'
            source.write_bytes(b'\xef\xbb\xbf#!/bin/sh\r\necho ok\r')
            prepare.copy_shell_lf(source, output)
            self.assertEqual(output.read_bytes(), b'#!/bin/sh\necho ok\n')
            self.assertEqual(output.stat().st_mode & 0o777, 0o755)
            source.write_bytes(b'echo missing shebang\n')
            with self.assertRaises(RuntimeError): prepare.copy_shell_lf(source, output)
    def test_modes_do_not_depend_on_host_filesystem(self):
        for name in ('cc2-control', 'start.sh', 'launch.sh', 'cc2-control.init', 'cc2-configure'):
            self.assertEqual(prepare.prepared_mode(name), 0o755)
        for name in ('web/index.html', 'web/locales/en.json', 'defaults/material-presets.json'):
            self.assertEqual(prepare.prepared_mode(name), 0o644)
if __name__ == '__main__': unittest.main()
