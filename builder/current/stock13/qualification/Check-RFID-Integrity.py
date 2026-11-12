"""Qualify scan hash consolidation before building a release candidate."""
import hashlib
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile

PROJECT = Path(__file__).resolve().parents[2]
SOURCE = PROJECT / 'work/cc2-13-port/source'
LIFECYCLE = SOURCE / 'experimental/canvas-rfid/lifecycle'
stock = PROJECT / 'work/cc2-stock-02.01.01.13/selected/opt/inst/ota/canvas_gd32-20251029_184624-00.00.00.44-app.bin'
assert hashlib.sha256(stock.read_bytes()).hexdigest() == 'f1267ee3219dcad73efffa8dc38e30ff0f17c02c0a1674cbead48a99bdff3228'
os.environ['CC2_TEST_STOCK_CANVAS'] = str(stock)
sys.path.insert(0, str(LIFECYCLE))
import test_scan_integrity
import test_post_boot

assert os.geteuid() == 0, 'Use WSL root for the root-private fixture files'
arm = '/opt/cc2-cross/toolchain-out/bin/arm-cortex_a15-linux-gnueabihf-gcc'
qemu = shutil.which('qemu-arm') or shutil.which('qemu-arm-static')
assert Path(arm).is_file() and qemu and shutil.which('gcc'), 'The existing GCC/QEMU toolchain is required'
architectures = [
    ('host', 'gcc', [], []),
    ('arm', arm, ['-static', '-D_TIME_BITS=32', '-D_FILE_OFFSET_BITS=32',
                   '-mcpu=cortex-a7', '-mfpu=neon-vfpv4', '-mfloat-abi=hard'], [qemu]),
    ('sanitizers', 'gcc', ['-fsanitize=address,undefined', '-fno-sanitize-recover=all', '-fno-pie', '-no-pie'], []),
]
test_scan_integrity.run(architectures)
# Staging deliberately rejects tmpfs/ramfs on the printer. WSL may mount /tmp
# as tmpfs, unlike the original offline container. Use real persistent storage
# for this fixture instead of suppressing or mocking the production check.
for path in ('/tmp', '/var/tmp'):
    result = subprocess.run(['stat', '-f', '-c', '%t', path], check=True,
                            capture_output=True, text=True, timeout=5)
    print(f'Legacy staging filesystem {path}: 0x{result.stdout.strip()}', flush=True)
with tempfile.TemporaryDirectory(prefix='cc2-persistent-regression-', dir='/var/tmp') as persistent:
    kind = subprocess.check_output(['stat', '-f', '-c', '%t', persistent], text=True, timeout=5).strip()
    assert int(kind, 16) not in (0x01021994, 0x858458f6), 'Legacy staging fixtures require non-RAM storage'
    test_post_boot.run(architectures=architectures, temporary_parent=persistent)
print('PASS RFID INTEGRITY OPTIMIZATION: host, ARM and sanitizer checks; no printer accessed', flush=True)
