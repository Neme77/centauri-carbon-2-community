from pathlib import Path
import importlib.util
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
        self.assertEqual(entries['gui/ec-eeb001-gui'][1], display.GUI_SHA256)
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

if __name__=='__main__': unittest.main()
