"""Qualify issue #267 without a printer, vendor program or real command socket."""
from pathlib import Path
import argparse
import os
import shutil
import subprocess
import tempfile

parser = argparse.ArgumentParser()
parser.add_argument('--baseline', type=Path)
args = parser.parse_args()
repo = Path(__file__).resolve().parents[2]
app = repo / 'cc2-control'
test = app / 'tests/test_pending_autosave.c'
flags = ['-O1', '-std=c11', '-Wall', '-Wextra', '-Wpedantic', '-Werror',
         '-D_POSIX_C_SOURCE=200809L', '-pthread']
backend = ['mqtt.c', 'console.c', 'control.c', 'panda.c', 'uds.c', 'network_status.c']

def run(command, **kwargs):
    print('+', *map(str, command), flush=True)
    return subprocess.run(list(map(str, command)), check=True, timeout=180, **kwargs)

with tempfile.TemporaryDirectory(prefix='cc2-save-config-') as temporary:
    base = Path(temporary)
    if args.baseline:
        baseline = args.baseline.resolve()
        shutil.copyfile(test, baseline / 'tests/test_pending_autosave.c')
        output = base / 'before'
        run(['gcc', *flags, '-DCC2_REPRODUCE_267', baseline / 'tests/test_pending_autosave.c',
             *[baseline / 'src' / name for name in backend], '-lm', '-o', output])
        run([output])
    for compiler, name, extra, runner in (
        ('gcc', 'host', [], []),
        ('arm-linux-gnueabihf-gcc', 'arm', ['-static'], ['qemu-arm']),
        ('gcc', 'sanitized', ['-g', '-fsanitize=address,undefined', '-fno-sanitize-recover=all'], []),
    ):
        output = base / name
        run([compiler, *flags, *extra, test, *[app / 'src' / file for file in backend], '-lm', '-o', output])
        run([*runner, output], env=dict(os.environ, ASAN_OPTIONS='detect_leaks=1'))
print('ISSUE #267 QUALIFICATION PASSED: host, ARM and address/undefined sanitizers. No printer accessed.', flush=True)
