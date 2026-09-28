#!/usr/bin/env python3
"""Compile and run the standalone C test harnesses that no other driver runs."""
import os
import pathlib
import subprocess
import tempfile

ROOT = pathlib.Path(__file__).resolve().parents[1]
FLAGS = ["-O2", "-std=c11", "-Wall", "-Wextra", "-Wpedantic", "-Werror", "-D_POSIX_C_SOURCE=200809L", "-pthread", *os.environ.get("CC2_TEST_CFLAGS", "").split()]
BACKEND = [ROOT / "src/mqtt.c", ROOT / "src/console.c", ROOT / "src/control.c", ROOT / "src/panda.c"]
HARNESSES = {
    # test_file_ops.c and test_orca_upload.c include src/main.c themselves.
    "test_file_ops.c": BACKEND,
    "test_orca_upload.c": BACKEND,
    "test_control_actions.c": [ROOT / "src/control.c"],
}

with tempfile.TemporaryDirectory(prefix="cc2-c-harness-") as temporary:
    for name, sources in HARNESSES.items():
        binary = pathlib.Path(temporary) / name[:-2]
        subprocess.run(["cc", *FLAGS, str(ROOT / "tests" / name), *map(str, sources), "-o", str(binary), "-lm"], check=True)
        subprocess.run([str(binary)], cwd=temporary, check=True, stdout=subprocess.DEVNULL)
        print(f"PASS: {name}")
