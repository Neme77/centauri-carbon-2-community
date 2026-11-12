"""Phase-5 helper update, same app: offline transaction and boot checks."""
import hashlib
import os
from pathlib import Path
import shutil
import subprocess
import sys

PROJECT = Path(__file__).resolve().parents[2]
LIFECYCLE = PROJECT / 'work/cc2-13-port/source/experimental/canvas-rfid/lifecycle'
stock = PROJECT / 'work/cc2-stock-02.01.01.13/selected/opt/inst/ota/canvas_gd32-20251029_184624-00.00.00.44-app.bin'
assert hashlib.sha256(stock.read_bytes()).hexdigest() == 'f1267ee3219dcad73efffa8dc38e30ff0f17c02c0a1674cbead48a99bdff3228'
assert os.geteuid() == 0, 'Run the private-file fixtures as WSL root'
arm = '/opt/cc2-cross/toolchain-out/bin/arm-cortex_a15-linux-gnueabihf-gcc'
qemu = shutil.which('qemu-arm') or shutil.which('qemu-arm-static')
assert qemu and Path(arm).is_file() and shutil.which('gcc')
os.environ['CC2_TEST_STOCK_CANVAS'] = str(stock)
sys.path.insert(0, str(LIFECYCLE))
import test_same_app_boot
architectures = [('host', 'gcc', [], []),
                 ('arm', arm, ['-static', '-D_TIME_BITS=32', '-D_FILE_OFFSET_BITS=32'], [qemu]),
                 ('sanitizers', 'gcc', ['-fsanitize=address,undefined', '-fno-sanitize-recover=all', '-fno-pie', '-no-pie'], [])]
env = {**os.environ, 'CC2_TEST_IMMUTABLE_BINDING': '1', 'CC2_TEST_PRESERVE_LIFECYCLE': '0',
       'CC2_TEST_BINDING_ALL_ARCHES': '1', 'CC2_TEST_ARM_CC': arm, 'CC2_TEST_QEMU': qemu}
for same_app in ('0', '1'):
    subprocess.run([sys.executable, '-u', '-B', str(LIFECYCLE / 'test_app_binding_tx.py')],
                   env={**env, 'CC2_TEST_SAME_APP_HELPERS': same_app}, check=True, timeout=240)
test_same_app_boot.run(architectures)
print('PASS ACTIVE RFID HELPER UPDATE: same-app and changed-app transactions, real boot worker; no printer accessed', flush=True)
