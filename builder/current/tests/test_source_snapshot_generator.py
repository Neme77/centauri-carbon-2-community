"""Snapshots use committed bytes even with Windows checkout conversion."""
import hashlib
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest
import zipfile

ROOT = Path(__file__).resolve().parents[3]

class SourceSnapshotGeneratorTest(unittest.TestCase):
    def test_checkout_conversion_and_dirty_refusal(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            def git(*args):
                return subprocess.run(['git', '-c', 'core.autocrlf=false', *args], cwd=root, check=True, capture_output=True)
            script = root / 'experimental/pr128-integration/sync_source.py'
            script.parent.mkdir(parents=True)
            shutil.copyfile(ROOT / 'experimental/pr128-integration/sync_source.py', script)
            source = root / 'cc2-control/test.c'
            source.parent.mkdir()
            canonical = b'int main(void) {return 0;}\n'
            source.write_bytes(canonical)
            (root / '.gitattributes').write_text('*.c text\n')
            builder = root / 'builder/current'
            (builder / 'components/cc2-control/source').mkdir(parents=True)
            for name, prefix in [('prepare.py', 'SOURCE'), ('core/firmware_builder.py', 'CC2_CONTROL_SOURCE')]:
                target = builder / name
                target.parent.mkdir(exist_ok=True)
                target.write_text(f"{prefix}_COMMIT='old'\n{prefix}_SHA256='old'\n")
            (builder / 'launch_helpers').mkdir()
            (builder / 'launch_helpers/run_build.py').write_text("BUILDER_SHA256='old'\n")
            git('init')
            git('add', '.')
            git('-c', 'user.name=Fixture', '-c', 'user.email=fixture@example.invalid', 'commit', '-m', 'fixture')
            source.write_bytes(canonical.replace(b'\n', b'\r\n'))
            result = subprocess.run([__import__('sys').executable, str(script)], cwd=root, capture_output=True)
            self.assertEqual(result.returncode, 0, result.stderr)
            with zipfile.ZipFile(builder / 'components/cc2-control/source/source.zip') as archive:
                self.assertEqual(archive.read('test.c'), canonical)
            generated = (builder / 'core/firmware_builder.py').read_bytes()
            self.assertNotIn(b'\r', generated)
            self.assertIn(hashlib.sha256(generated).hexdigest(), (builder / 'launch_helpers/run_build.py').read_text())
            source.write_bytes(b'changed\n')
            result = subprocess.run([__import__('sys').executable, str(script)], cwd=root, capture_output=True)
            self.assertNotEqual(result.returncode, 0)
