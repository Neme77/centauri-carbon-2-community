"""CLI regression tests using synthetic files; no vendor binaries are required."""
import hashlib
import json
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import unittest


PATCH_DIR = Path(__file__).resolve().parents[3] / 'experimental/canvas-reverse'


class CanvasReversePatcherTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name)
        self.patcher = self.root / 'patch_library.py'
        shutil.copyfile(PATCH_DIR / 'patch_library.py', self.patcher)
        production = json.loads((PATCH_DIR / 'manifest.json').read_text())
        entry = next(x for x in production['libraries'] if x['ota_version'] == '02.01.00.00')
        self.before = bytes.fromhex(entry['original_hex'])
        self.after = bytes.fromhex(entry['patched_hex'])
        prefix, suffix = b'synthetic-prefix', b'synthetic-suffix'
        self.original = prefix + self.before + suffix
        self.fixed = prefix + self.after + suffix
        # This test-only manifest makes the tiny synthetic file recognizable.
        # The production manifest and its vendor hashes remain unchanged.
        self.entry = dict(entry, file_offset=len(prefix),
                          input_sha256=hashlib.sha256(self.original).hexdigest(),
                          output_sha256=hashlib.sha256(self.fixed).hexdigest())
        self.manifest = self.root / 'manifest.json'
        self.write_manifest()
        self.source = self.root / 'original.so'
        self.source.write_bytes(self.original)
        self.output = self.root / 'fixed.so'

    def write_manifest(self):
        self.manifest.write_text(json.dumps({'schema': 1, 'libraries': [self.entry]}))

    def run_patcher(self, *args, expected=0):
        result = subprocess.run([sys.executable, str(self.patcher), '--input', str(self.source),
                                 *map(str, args)], capture_output=True, text=True)
        self.assertEqual(result.returncode, expected, result.stdout + result.stderr)
        self.assertEqual(self.source.read_bytes(), self.original)
        return result

    def test_check_only_preserves_input_and_creates_no_output(self):
        result = self.run_patcher('--version', '02.01.00.00')
        self.assertIn('Check only; no files changed.', result.stdout)
        self.assertFalse(self.output.exists())

    def test_apply_changes_only_the_intended_block(self):
        self.run_patcher('--version', '02.01.00.00', '--apply', '--output', self.output)
        self.assertEqual(self.output.read_bytes(), self.fixed)
        self.assertEqual(len(self.fixed), len(self.original))

    def test_existing_output_is_preserved(self):
        self.output.write_bytes(b'existing output')
        self.run_patcher('--apply', '--output', self.output, expected=2)
        self.assertEqual(self.output.read_bytes(), b'existing output')

    def test_input_overwrite_is_refused(self):
        self.run_patcher('--apply', '--output', self.source, expected=2)

    def test_unknown_and_truncated_inputs_are_refused(self):
        for data in (b'unknown input', self.original[:8]):
            with self.subTest(data=data):
                self.original = data
                self.source.write_bytes(data)
                result = self.run_patcher('--apply', '--output', self.output, expected=2)
                self.assertIn('unknown input SHA-256', result.stderr)
                self.assertFalse(self.output.exists())

    def test_already_patched_input_cannot_be_applied_twice(self):
        self.original = self.fixed
        self.source.write_bytes(self.fixed)
        self.assertIn('already contains this correction', self.run_patcher().stdout)
        self.run_patcher('--apply', '--output', self.output, expected=2)
        self.assertFalse(self.output.exists())

    def test_wrong_requested_version_is_refused(self):
        self.run_patcher('--version', '02.00.02.00', '--apply', '--output', self.output, expected=2)
        self.assertFalse(self.output.exists())

    def test_instruction_precondition_is_checked_after_hash(self):
        self.entry['original_hex'] = bytes(24).hex()
        self.write_manifest()
        result = self.run_patcher('--apply', '--output', self.output, expected=2)
        self.assertIn('instruction precondition failed', result.stderr)
        self.assertFalse(self.output.exists())

    def test_incorrect_expected_output_hash_is_refused_before_writing(self):
        self.entry['output_sha256'] = '0' * 64
        self.write_manifest()
        result = self.run_patcher('--apply', '--output', self.output, expected=2)
        self.assertIn('expected output checksum mismatch', result.stderr)
        self.assertFalse(self.output.exists())

    def test_apply_and_output_must_be_supplied_together(self):
        self.run_patcher('--apply', expected=2)
        self.run_patcher('--output', self.output, expected=2)
        self.assertFalse(self.output.exists())


if __name__ == '__main__':
    unittest.main()
