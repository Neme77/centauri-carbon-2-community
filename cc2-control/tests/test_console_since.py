#!/usr/bin/env python3
import os
import pathlib
import subprocess
import tempfile

ROOT = pathlib.Path(__file__).resolve().parents[1]

with tempfile.TemporaryDirectory(prefix="cc2-console-since-test-") as temporary:
    binary = pathlib.Path(temporary) / "test-console-since"
    subprocess.run([
        "cc", "-O2", "-std=c11", "-Wall", "-Wextra", "-Wpedantic",
        "-D_POSIX_C_SOURCE=200809L", "-pthread", *os.environ.get("CC2_TEST_CFLAGS", "").split(),
        str(ROOT / "tests/test_console_since.c"), "-o", str(binary),
    ], check=True)
    subprocess.run([str(binary)], check=True)
