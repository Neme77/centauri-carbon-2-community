"""Run the production USB policy against sysfs stand-ins on host and ARM."""
from pathlib import Path
import subprocess
import tempfile

root = Path(__file__).resolve().parent
for name, compiler, runner in (
    ('host', 'gcc', []),
    ('arm', 'arm-linux-gnueabihf-gcc', ['qemu-arm']),
):
    with tempfile.TemporaryDirectory(prefix='cc2-usb-policy-') as directory:
        base = Path(directory)
        flags = ['-std=c11', '-Os', '-Wall', '-Wextra', '-Wpedantic', '-Werror', '-static', '-DNG_TEST']
        flags += [f'-DNG_{key}="{value}"' for key, value in {
            'STATE': base / 'state', 'RUN': base / 'run', 'USB_SYSFS': base / 'usb',
        }.items()]
        binary = base / 'policy'
        subprocess.run([compiler, *flags, root / 'test_usb_policy.c', '-o', binary], check=True, timeout=90)
        subprocess.run([*runner, binary], check=True, timeout=30)
        print(name, 'USB policy passed', flush=True)
