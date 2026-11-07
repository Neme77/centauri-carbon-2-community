"""A delayed application turn must read queued status before expiring UDS."""
from pathlib import Path
import os
import shlex
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
with tempfile.TemporaryDirectory(prefix="cc2-uds-timeout-") as directory:
    binary = Path(directory) / "test"
    command = [os.environ.get("CC", "gcc"), "-O2", "-std=c11",
               "-D_POSIX_C_SOURCE=200809L", "-Wall", "-Wextra", "-Wpedantic", "-Werror",
               *shlex.split(os.environ.get("CC2_TEST_CFLAGS", "")),
               str(root / "tests/test_uds_timeout.c"), str(root / "src/uds.c"),
               "-lm", "-o", str(binary)]
    subprocess.run(command, check=True, timeout=120)
    subprocess.run([str(binary)], check=True, timeout=15)
