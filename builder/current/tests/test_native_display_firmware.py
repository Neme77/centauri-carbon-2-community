from pathlib import Path
import importlib.util
import json
import tempfile
import unittest
from unittest.mock import patch

FILE=Path(__file__).resolve().parents[1]/'core/native_display_component.py'
spec=importlib.util.spec_from_file_location('native_firmware_display', FILE)
display=importlib.util.module_from_spec(spec);spec.loader.exec_module(display)

class NativeFirmwareDisplayTests(unittest.TestCase):
    def test_complete_reviewed_resource_set(self):
        entries=display.pins()
        self.assertGreater(len(entries), 400)
        self.assertEqual(entries['gui/ec-eeb001-gui'][1], display.DISPLAY_GUI_SHA256)
        thumbnail_plan = json.loads((display.SOURCE / 'thumbnail07.json').read_bytes())
        self.assertEqual(thumbnail_plan['output_sha256'], display.GUI_SHA256)
        self.assertIn('images/home_menu_illustration_printer_light_off.png', entries)
        self.assertIn('images/home_menu_illustration_printer_light_on.png', entries)

    def test_unknown_baseline_fails_before_packager_or_writes(self):
        with tempfile.TemporaryDirectory() as temporary:
            root=Path(temporary)
            gui=root/'opt/bin/ec-eeb001-gui';gui.parent.mkdir(parents=True)
            gui.write_bytes(b'unknown')
            with patch.object(display.subprocess, 'run') as run:
                with self.assertRaisesRegex(RuntimeError, 'baseline'):
                    display.install(root)
                run.assert_not_called()
            self.assertEqual(gui.read_bytes(), b'unknown')
            self.assertFalse((root/'opt/usr').exists())

    def test_missing_resource_refused(self):
        with tempfile.TemporaryDirectory() as temporary:
            with self.assertRaisesRegex(RuntimeError, 'Missing'):
                display.target(Path(temporary), 'gui/ec-eeb001-gui')

    def test_audit_detects_changed_output(self):
        with tempfile.TemporaryDirectory() as temporary:
            root=Path(temporary);gui=root/'opt/bin/ec-eeb001-gui'
            gui.parent.mkdir(parents=True);gui.write_bytes(b'changed')
            with self.assertRaisesRegex(RuntimeError, 'audit failed'):
                display.audit(root)

    def test_display_manifest_is_bounded_and_role_scoped(self):
        manifest = display.display_manifest()
        self.assertEqual(manifest['schema'], 1)
        self.assertEqual(manifest['guiSha256'], display.GUI_SHA256)
        self.assertLess(len(manifest['assets']), 320)
        names = {asset['name'] for asset in manifest['assets']}
        self.assertIn('common_background_home_menu.png', names)
        self.assertIn('0.png', names)
        self.assertNotIn('home_menu_illustration_printer_light_off.png', names)
        roles = {asset['role'] for asset in manifest['assets']}
        self.assertEqual(roles, {'boot', 'background', 'accent'})
        for asset in manifest['assets']:
            self.assertRegex(asset['sha256'], r'^[a-f0-9]{64}$')
            self.assertGreater(asset['width'], 0)
            self.assertGreater(asset['height'], 0)
            self.assertNotIn('/', asset['name'])
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            folder = root / 'opt/inst/images/theme_default'
            folder.mkdir(parents=True)
            path = display.write_manifest(root)
            self.assertTrue(path.is_file())
            self.assertLessEqual(path.stat().st_size, 32768)
            self.assertEqual(json.loads(path.read_text(encoding='utf-8'))['assets'],
                             manifest['assets'])

if __name__=='__main__': unittest.main()
