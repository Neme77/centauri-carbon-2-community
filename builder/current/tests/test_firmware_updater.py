"""Fresh-firmware updater must have its entire trusted runtime in rootfs."""
import importlib.util
from pathlib import Path
import struct
from unittest.mock import Mock
import unittest

BASE = Path(__file__).resolve().parents[1]
def module(name, path):
    spec = importlib.util.spec_from_file_location(name, path)
    value = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(value)
    return value

b = module('firmware_updater_builder', BASE/'core/firmware_builder.py')
p = module('firmware_updater_prepare', BASE/'prepare.py')

class FirmwareUpdater(unittest.TestCase):
    def test_helpers_are_required_as_a_complete_set(self):
        base = dict.fromkeys(b.CC2_CONTROL_FILES)
        with self.assertRaisesRegex(RuntimeError, 'updater'):
            b.cc2_control_files(base)
        for missing in b.CC2_UPDATE_FILES:
            files = dict.fromkeys(b.CC2_CONTROL_FILES+b.CC2_UPDATE_FILES)
            del files[missing]
            with self.assertRaisesRegex(RuntimeError, 'updater'):
                b.cc2_control_files(files)

    def test_rootfs_paths_and_modes(self):
        files = dict.fromkeys(b.CC2_CONTROL_FILES+b.CC2_UPDATE_FILES)
        destinations = b.cc2_control_destinations(Path('/image'), {'files': files})
        self.assertEqual(set(destinations), set(files))
        for name in b.CC2_UPDATE_FILES:
            self.assertEqual(destinations[name], Path('/image/opt/inst/cc2-control')/name)
            self.assertEqual(p.prepared_mode(name), 0o755 if name in
                             ('cc2-update-worker','cc2-https-download','update-runner.sh') else 0o644)
        start = (BASE/'components/cc2-control/runtime/start.sh').read_text()
        self.assertIn('--plates "$PERSIST/bed-plates.json"', start)
        for flag, name in (('verifier','cc2-update-worker'), ('downloader','cc2-https-download'),
                           ('ca','ca-bundle.pem'), ('runner','update-runner.sh')):
            self.assertIn(f'--update-{flag} "$BASE/{name}"', start)

    def test_helper_architecture_checked_without_app_version_string(self):
        path = Mock()
        elf = bytearray(64)
        elf[:6] = b'\x7fELF\x01\x01'
        struct.pack_into('<H', elf, 18, 40)
        path.read_bytes.return_value = bytes(elf)
        p.validate_arm_elf(path, require_version=False)
        with self.assertRaisesRegex(RuntimeError, 'version'):
            p.validate_arm_elf(path)
        struct.pack_into('<H', elf, 18, 62)
        path.read_bytes.return_value = bytes(elf)
        with self.assertRaisesRegex(RuntimeError, 'ARM'):
            p.validate_arm_elf(path, require_version=False)

    def test_release_number_does_not_change_compatibility_family(self):
        self.assertEqual(b.firmware_release_version(), '4.4.2')
        self.assertEqual(b.firmware_version(), '4.4')

if __name__ == '__main__':
    unittest.main()

