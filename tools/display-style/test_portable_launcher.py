import importlib.util
import io
import json
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch

spec = importlib.util.spec_from_file_location('launcher', Path(__file__).with_name('printer.py'))
launcher = importlib.util.module_from_spec(spec)
spec.loader.exec_module(launcher)


class LauncherTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.home = Path(self.temp.name)
        (self.home / 'display-kit.tar.gz').write_bytes(b'fixture')
        self.bundle = launcher.hashlib.sha256(b'fixture').hexdigest()
        self.addCleanup(patch.stopall)
        patch.object(launcher, 'BUNDLE', self.bundle).start()
        patch('builtins.input', return_value='192.0.2.10').start()
        patch('sys.stdout', new=io.StringIO()).start()
        self.remote = patch.object(launcher, 'command').start()

    def record(self, **changes):
        data = dict(ip='192.0.2.10', stage='/opt/usr/cc2-stock-style-' + 'a' * 32,
                    bundle_sha256=self.bundle, read_only_passed=True)
        data.update(changes)
        launcher.save_record(self.home / 'printer-stage.json', data)

    def test_check_stages_and_never_applies(self):
        launcher.run('Check', self.home)
        self.assertEqual(self.remote.call_count, 3)
        self.assertEqual(self.remote.call_args_list[0].args[0], 'scp')
        commands = str(self.remote.call_args_list)
        self.assertNotIn('--apply', commands)
        self.assertNotIn('--restore', commands)
        self.assertTrue(json.loads((self.home / 'printer-stage.json').read_text())['read_only_passed'])

    def test_check_reuses_exact_stage(self):
        self.record()
        launcher.run('Check', self.home)
        self.assertEqual(self.remote.call_count, 1)

    def test_explicit_install_and_restore(self):
        self.record()
        for action, mode in [('Install', '--apply'), ('Restore', '--restore'), ('Status', '--status')]:
            launcher.run(action, self.home)
            self.assertIn("'" + mode + "'", self.remote.call_args.args[1][-1])

    def test_missing_check_refuses_without_network(self):
        with self.assertRaises(RuntimeError):
            launcher.run('Install', self.home)
        self.remote.assert_not_called()

    def test_tampered_bundle_refuses_without_network(self):
        (self.home / 'display-kit.tar.gz').write_bytes(b'changed')
        with self.assertRaises(RuntimeError):
            launcher.run('Check', self.home)
        self.remote.assert_not_called()

    def test_invalid_ip_refuses_without_network(self):
        with patch('builtins.input', return_value='192.0.2.10; touch /bad'):
            with self.assertRaises(ValueError):
                launcher.run('Check', self.home)
        self.remote.assert_not_called()

    def test_wrong_kit_refuses_without_network(self):
        self.record(bundle_sha256='wrong')
        with self.assertRaises(RuntimeError):
            launcher.run('Restore', self.home)
        self.remote.assert_not_called()

    def test_invalid_stage_refuses_without_network(self):
        self.record(stage='/opt/usr/cc2-stock-style-x; reboot')
        with self.assertRaises(RuntimeError):
            launcher.run('Install', self.home)
        self.remote.assert_not_called()

    def test_remote_failure_leaves_admission_false(self):
        self.remote.side_effect = [None, RuntimeError('fixture error')]
        with self.assertRaises(RuntimeError):
            launcher.run('Check', self.home)
        self.assertFalse(json.loads((self.home / 'printer-stage.json').read_text())['read_only_passed'])

    def test_transfer_failure_does_not_create_receipt(self):
        self.remote.side_effect = RuntimeError('fixture transfer')
        with self.assertRaises(RuntimeError):
            launcher.run('Check', self.home)
        self.assertFalse((self.home / 'printer-stage.json').exists())


if __name__ == '__main__':
    unittest.main()
