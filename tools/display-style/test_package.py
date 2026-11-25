import importlib.util
from pathlib import Path
import tempfile
import unittest

spec = importlib.util.spec_from_file_location('style_package', Path(__file__).with_name('package.py'))
package = importlib.util.module_from_spec(spec)
spec.loader.exec_module(package)


class PackageTests(unittest.TestCase):
    def test_unqualified_payload_is_refused_before_output(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            source = root / 'bundle.tar.gz'
            source.write_bytes(b'unqualified payload')
            with self.assertRaisesRegex(ValueError, 'Unqualified payload'):
                package.build(source, root / 'output')
            self.assertFalse((root / 'output').exists())

    def test_binding_pins_match_both_launchers(self):
        for name in ('printer.py', 'Printer.ps1'):
            text = (package.H / name).read_text(encoding='utf-8')
            self.assertIn(package.BUNDLE_SHA, text)
            self.assertIn(package.digest((package.H / 'install.sh').read_bytes()), text)


if __name__ == '__main__':
    unittest.main()
