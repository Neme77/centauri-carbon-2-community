"""Real reference validation plus transactional gate tests (no production keys)."""
import hashlib
import importlib.util
import tempfile
import unittest
from pathlib import Path
from unittest.mock import patch

BASE = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location('builder38', BASE / 'cc2_firmware_builder_v3.8.py')
b = importlib.util.module_from_spec(spec)
spec.loader.exec_module(b)

class ReferenceTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.data = b.PRINTER_RELEASE_DEFAULT.read_bytes()

    def test_real_reference_and_webcam_reversal(self):
        self.assertEqual(b.validate_release_printer(self.data), self.data)
        prior = bytearray(self.data)
        prior[0x5b3ca8:0x5b3cac] = bytes.fromhex('d5d6feeb')
        self.assertEqual(hashlib.sha256(prior).hexdigest(),
                         '0304ea0cbef4e63baa32f98680076c683c18c0d9255c1c66ac3c3b5e4262ff6a')
        with self.assertRaises(RuntimeError):
            b.validate_release_printer(prior)  # MQTT v2 alone is not V3.8.

    def test_reject_mutated_truncated_extended(self):
        mutated = bytearray(self.data)
        mutated[-1] ^= 1
        for data in [b'', self.data[:-1], self.data + b'X', mutated]:
            with self.assertRaises(RuntimeError):
                b.validate_release_printer(data)

    def test_unknown_intermediate_does_not_change_file(self):
        with tempfile.TemporaryDirectory() as directory:
            printer = Path(directory) / 'printer'
            printer.write_bytes(b'unknown input')
            with self.assertRaises(RuntimeError):
                b.install_release_printer(printer, b.PRINTER_RELEASE_DEFAULT)
            self.assertEqual(printer.read_bytes(), b'unknown input')

    def test_copy_and_bad_reference_are_transactional(self):
        # Synthetic intermediate exercises I/O only; NOT an end-to-end stock build.
        fixture = b'synthetic HTTP stage for I/O test'
        with tempfile.TemporaryDirectory() as directory:
            printer = Path(directory) / 'printer'
            candidate = Path(directory) / 'bad'
            printer.write_bytes(fixture)
            printer.chmod(0o775)
            candidate.write_bytes(b'invalid reference')
            real_reqhash = b.reqhash
            def fixture_gate(path, expected, label):
                if Path(path) == printer and expected == b.PRINTER_PATCHED_SHA256:
                    expected = hashlib.sha256(fixture).hexdigest()
                return real_reqhash(path, expected, label)
            with patch.object(b, 'reqhash', side_effect=fixture_gate):
                with self.assertRaises(RuntimeError):
                    b.install_release_printer(printer, candidate)
                self.assertEqual(printer.read_bytes(), fixture)
                b.install_release_printer(printer, b.PRINTER_RELEASE_DEFAULT)
            self.assertEqual(printer.read_bytes(), self.data)
            self.assertEqual(printer.stat().st_mode & 0o777, 0o775)

    def test_existing_trust_and_component_pins_unchanged(self):
        prior = BASE.parent / 'cc2_builder_v3_7'
        if not prior.exists():
            self.skipTest('V3.7 sources not present in this standalone delivery')
        oldspec = importlib.util.spec_from_file_location('builder37', prior / 'cc2_firmware_builder_v3.7.py')
        old = importlib.util.module_from_spec(oldspec)
        oldspec.loader.exec_module(old)
        self.assertEqual(b.EXPECTED, old.EXPECTED)
        self.assertEqual(b.PUBLIC_HASHES, old.PUBLIC_HASHES)
        for path in ['keys/cc2_stock_public.pem', 'dualtrust/cc2_community_release_public.pem',
                     'dualtrust/apply_dualtrust.py', 'dualtrust/dual_verify.S',
                     'components/ssh/sshd.init', 'components/ssh/sshd_config']:
            self.assertEqual((BASE/path).read_bytes(), (prior/path).read_bytes())

if __name__ == '__main__':
    unittest.main()
