import hashlib
import os
from pathlib import Path
import struct
import tempfile
import unittest

import patch_pullback as patch


class RefusalTests(unittest.TestCase):
    def test_constants_are_distances(self):
        self.assertEqual(struct.unpack('<d', patch.ORIGINAL)[0], 350.0)
        self.assertEqual(struct.unpack('<d', patch.EXTENDED)[0], 1000.0)
        self.assertEqual(sum(a != b for a, b in zip(patch.ORIGINAL, patch.EXTENDED)), 2)

    def test_wrong_input_never_creates_output(self):
        with tempfile.TemporaryDirectory() as folder:
            source, destination = Path(folder) / 'input', Path(folder) / 'output'
            source.write_bytes(b'unsupported executable')
            with self.assertRaises(ValueError):
                patch.apply(source, destination)
            self.assertFalse(destination.exists())
            self.assertEqual(source.read_bytes(), b'unsupported executable')

    def test_size_alone_does_not_authorize(self):
        data = bytearray(patch.SIZE)
        data[patch.OFFSET:patch.OFFSET + 8] = patch.ORIGINAL
        with self.assertRaises(ValueError):
            patch.transform(bytes(data))

    def test_reverse_also_requires_exact_fingerprint(self):
        with self.assertRaises(ValueError):
            patch.transform(b'unsupported executable', reverse=True)


@unittest.skipUnless(os.environ.get('CC2_172_PRIVATE_INPUT'), 'Qualified private input is never committed or required on public CI')
class PrivateInputTests(unittest.TestCase):
    def setUp(self):
        self.original = Path(os.environ['CC2_172_PRIVATE_INPUT']).read_bytes()

    def test_exact_patch_and_reverse(self):
        extended = patch.transform(self.original)
        self.assertEqual(hashlib.sha256(extended).hexdigest(), patch.EXTENDED_SHA256)
        self.assertEqual([i for i, (a, b) in enumerate(zip(self.original, extended)) if a != b], [0x5BBA3D, 0x5BBA3E])
        self.assertEqual(patch.transform(extended, reverse=True), self.original)
        with self.assertRaises(ValueError):
            patch.transform(extended)

    def test_no_overwrite_and_input_unchanged(self):
        with tempfile.TemporaryDirectory() as folder:
            source, destination = Path(folder) / 'input', Path(folder) / 'output'
            source.write_bytes(self.original)
            destination.write_bytes(b'preserve output')
            with self.assertRaises(FileExistsError):
                patch.apply(source, destination)
            self.assertEqual(destination.read_bytes(), b'preserve output')
            self.assertEqual(source.read_bytes(), self.original)

    def test_unrelated_change_and_truncation_refused(self):
        damaged = bytearray(self.original)
        damaged[0x1000] ^= 1
        for data in [bytes(damaged), self.original[:-1]]:
            with self.assertRaises(ValueError):
                patch.transform(data)


if __name__ == '__main__':
    unittest.main()
