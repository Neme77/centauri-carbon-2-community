#!/usr/bin/env python3
import os
import pathlib
import subprocess
import tempfile

ROOT = pathlib.Path(__file__).resolve().parents[1]

with tempfile.TemporaryDirectory(prefix="cc2-lane-data-test-") as temporary:
    binary = pathlib.Path(temporary) / "test-lane-data"
    subprocess.run([
        "cc", "-O2", "-std=c11", "-Wall", "-Wextra", "-Wpedantic",
        "-D_POSIX_C_SOURCE=200809L", "-pthread", *os.environ.get("CC2_TEST_CFLAGS", "").split(),
        str(ROOT / "tests/test_orca_lane_data.c"),
        str(ROOT / "src/mqtt.c"), str(ROOT / "src/console.c"),
        str(ROOT / "src/control.c"), str(ROOT / "src/panda.c"),
        "-o", str(binary), "-lm",
    ], check=True)
    subprocess.run([str(binary)], check=True)
