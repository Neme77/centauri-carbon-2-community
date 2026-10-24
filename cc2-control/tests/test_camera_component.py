"""Boot-only camera component guards and rollback on two qualified profiles."""
import os
from pathlib import Path
import subprocess
import sys

test=Path(__file__).resolve().parents[1]/'installer/camera/test_persistent.py'
for name in ('common.sh','manage.sh','boot-camera.sh'):
    data=(test.parent/name).read_bytes()
    assert data.startswith(b'#!/bin/sh\n') and b'\r' not in data, name+' must be canonical LF shell source'
for native in ('0','1'):
    subprocess.run([sys.executable,str(test)],check=True,
                   env=dict(os.environ,CC2_TEST_NATIVE_PROFILE=native))
