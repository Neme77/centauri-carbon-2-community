#!/usr/bin/env python3
"""Compile and run test_cavity_floor_api.c with its channel and component in a temporary directory.

The cavity floor files must belong to root (cavity_stat checks the owner), so the harness runs only as
root: the CI runs this script with `sudo -n`, and `make test` as a normal user skips it with a message.
"""
import json
import os
import pathlib
import subprocess
import sys
import tempfile

ROOT = pathlib.Path(__file__).resolve().parents[1]
FLAGS = ["-O2", "-std=c11", "-Wall", "-Wextra", "-Wpedantic", "-Werror", "-D_POSIX_C_SOURCE=200809L", "-pthread", *os.environ.get("CC2_TEST_CFLAGS", "").split()]
BACKEND = [ROOT / "src/mqtt.c", ROOT / "src/console.c", ROOT / "src/control.c", ROOT / "src/panda.c", ROOT / "src/uds.c", ROOT / "src/network_status.c"]

if not sys.platform.startswith("linux") or os.geteuid() != 0:
    print("SKIP: test_cavity_floor_api.c needs Linux and root, because the cavity floor files must be root-owned (CI: sudo -n python3 tests/test_cavity_floor_api.py)")
    sys.exit(0)

with tempfile.TemporaryDirectory(prefix="cc2-cavity-floor-") as temporary:
    work = pathlib.Path(temporary)
    binary = work / "test_cavity_floor_api"
    defines = [
        "-DCC2_CAVITY_CHANNEL=" + json.dumps(str(work / "channel")),
        "-DCC2_CAVITY_COMPONENT=" + json.dumps(str(work / "component")),
    ]
    subprocess.run(["cc", *FLAGS, *defines, str(ROOT / "tests/test_cavity_floor_api.c"), *map(str, BACKEND), "-o", str(binary), "-lm"], check=True)
    subprocess.run([str(binary)], cwd=temporary, check=True)
    print("PASS: test_cavity_floor_api.c")
