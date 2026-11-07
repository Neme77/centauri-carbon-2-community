# SPDX-License-Identifier: GPL-3.0-or-later
"""Builds and runs test_nui.c on the host, with sanitizers when available, and
checks that nui.c compiles warning-free for the printer when the ARM compiler is
installed. Neither step needs the vendor program."""
import os
import shutil
import subprocess
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
WARN = ['-std=c11', '-Wall', '-Wextra', '-Werror']


def main():
    with tempfile.TemporaryDirectory() as d:
        exe = os.path.join(d, 'test_nui')
        flags = ['-g', '-O1', '-fsanitize=address,undefined', '-fno-sanitize-recover=all']
        probe = subprocess.run(['cc', *flags, '-x', 'c', '-', '-o', os.path.join(d, 'probe')],
                               input=b'int main(void){return 0;}', capture_output=True)
        if probe.returncode:
            flags = ['-O1']
        subprocess.run(['cc', *WARN, '-Wno-unused-function', *flags, '-I', HERE, os.path.join(HERE, 'test_nui.c'), '-o', exe], check=True)
        subprocess.run([exe], check=True, env=dict(os.environ, ASAN_OPTIONS='detect_leaks=0'))
        cross = os.environ.get('CROSS', 'arm-linux-gnueabihf-') + 'gcc'
        if shutil.which(cross):
            sys.path.insert(0, HERE)
            import build
            subprocess.run([cross, *build.CFLAGS, '-c', os.path.join(HERE, 'nui.c'), '-o', os.path.join(d, 'nui.o')], check=True)
            print('PASS ARM compile')
        else:
            print('SKIP ARM compile:', cross, 'not installed')


if __name__ == '__main__':
    main()
