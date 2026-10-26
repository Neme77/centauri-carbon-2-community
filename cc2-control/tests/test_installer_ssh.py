import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

INSTALLER = Path(__file__).resolve().parents[1] / "installer" / "install-cc2-control.sh"


class InstallerSshTests(unittest.TestCase):
    def run_installer(self, arguments, confirmation="", keygen_exit=0):
        with tempfile.TemporaryDirectory(prefix="cc2-installer-test-") as directory:
            root = Path(directory)
            script = root / INSTALLER.name
            shutil.copyfile(INSTALLER, script)
            (root / "cc2-control-payload.tar.gz").touch()
            tools = root / "tools"
            tools.mkdir()
            trace = root / "trace"
            for name in ("ssh", "scp", "ssh-keygen"):
                tool = tools / name
                tool.write_text(
                    '#!/bin/sh\n'
                    'printf "%s" "${0##*/}" >> "$CC2_TEST_TRACE"\n'
                    'for arg do printf " <%s>" "$arg" >> "$CC2_TEST_TRACE"; done\n'
                    'printf "\\n" >> "$CC2_TEST_TRACE"\n'
                    'if [ "${0##*/}" = ssh-keygen ]; then\n'
                    '    exit "$CC2_TEST_KEYGEN_EXIT"\n'
                    'fi\n'
                    'exit 0\n',
                    encoding="utf-8",
                )
                tool.chmod(0o755)
            env = dict(os.environ)
            env["PATH"] = str(tools) + os.pathsep + env["PATH"]
            env["CC2_TEST_TRACE"] = str(trace)
            env["CC2_TEST_KEYGEN_EXIT"] = str(keygen_exit)
            result = subprocess.run(
                ["sh", str(script), *arguments],
                input=confirmation,
                text=True,
                capture_output=True,
                env=env,
                timeout=10,
            )
            calls = trace.read_text().splitlines() if trace.exists() else []
            return result, calls

    def test_normal_install_does_not_reset_key(self):
        result, calls = self.run_installer(["192.0.2.10"])
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual([line.split()[0] for line in calls], ["scp", "ssh"])

    def test_confirmed_reset_runs_before_upload(self):
        result, calls = self.run_installer(
            ["--reset-host-key", "192.0.2.10"], "192.0.2.10\n"
        )
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(calls[0], "ssh-keygen <-R> <192.0.2.10>")
        self.assertEqual([line.split()[0] for line in calls], ["ssh-keygen", "scp", "ssh"])

    def test_cancel_does_not_reset_or_upload(self):
        result, calls = self.run_installer(
            ["--reset-host-key", "192.0.2.10"], "no\n"
        )
        self.assertNotEqual(result.returncode, 0)
        self.assertEqual(calls, [])

    def test_eof_does_not_reset_or_upload(self):
        result, calls = self.run_installer(["--reset-host-key", "192.0.2.10"])
        self.assertNotEqual(result.returncode, 0)
        self.assertEqual(calls, [])

    def test_reset_failure_blocks_upload(self):
        result, calls = self.run_installer(
            ["--reset-host-key", "192.0.2.10"], "192.0.2.10\n", keygen_exit=1
        )
        self.assertNotEqual(result.returncode, 0)
        self.assertEqual(calls, ["ssh-keygen <-R> <192.0.2.10>"])


if __name__ == "__main__":
    unittest.main(argv=[str(INSTALLER)], verbosity=2)
