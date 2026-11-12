"""Build only the additional 3000 probe from the same pinned R1 GPL inputs.

No module is loaded. The existing Ethernet driver and qualified 31fd probe are
reused unchanged; this build does not establish physical USB qualification.
"""
from pathlib import Path
import hashlib
import json
import os
import shutil
import subprocess
import sys
import urllib.request

source = Path(__file__).resolve().parent
output = Path(sys.argv[1]).resolve()
output.mkdir(parents=True, exist_ok=False)
inputs = (
    ('kernel', 'oc-linux', 'cd4ee0cf38af4f4b21f2918e2ad9f0627fdfde57',
     'bbb7a3563a0e28d66383671f20a3f92957b473be8c29da78ce55724c1f52af60'),
    ('toolchain', 'arm-openwrt-linux-gnueabi-gcc', 'f4210c9d613a5e1cb4fc07a1b03bc35ab332f596',
     '57981908b316f4e5a0810d0568f54da90b5d1bad88dc3b6ff3a941deb9fa205a'),
)

def run(args, **kwargs):
    print('+', *map(str, args), flush=True)
    return subprocess.run(list(map(str, args)), check=True, timeout=600, **kwargs)

for name, repository, commit, digest in inputs:
    archive = output / (name + '.tar.gz')
    urllib.request.urlretrieve(f'https://codeload.github.com/OpenCentauri/{repository}/tar.gz/{commit}', archive)
    with archive.open('rb') as stream:
        assert hashlib.file_digest(stream, 'sha256').hexdigest() == digest, name
    target = output / name
    target.mkdir()
    # These archives are the exact hash-verified GPL inputs of the R1 build.
    run(['tar', '-xzf', archive, '-C', target, '--strip-components=1'])

kernel, sdk = output / 'kernel', output / 'toolchain'
run(['bash', './00_repair_symlink'], cwd=sdk)
run(['bash', './01_make_executable'], cwd=sdk, stdout=subprocess.DEVNULL)
os.environ['PATH'] = str(sdk / 'bin') + os.pathsep + os.environ['PATH']
os.environ['STAGING_DIR'] = str(sdk)
os.environ['KBUILD_BUILD_TIMESTAMP'] = 'Thu Jan 1 00:00:00 UTC 1970'
os.environ['KBUILD_BUILD_USER'] = 'cc2'
os.environ['KBUILD_BUILD_HOST'] = 'qualification'
cross = 'arm-openwrt-linux-gnueabi-'
assert run([cross + 'gcc', '-dumpversion'], capture_output=True, text=True).stdout.strip() == '6.4.1'
shutil.copyfile(source / 'printer.config', kernel / '.config')
run(['scripts/config', '--enable', 'USB_NET_DRIVERS', '--module', 'MII', '--module', 'USB_RTL8152'], cwd=kernel)
make = ['make', '-C', kernel, 'ARCH=arm', 'CROSS_COMPILE=' + cross, 'HOSTCFLAGS=-O2 -fcommon', 'EXTRAVERSION=']
run([*make, 'olddefconfig'])
run([*make, '-j2', 'modules_prepare'])
assert run([*make, '-s', 'kernelrelease'], capture_output=True, text=True).stdout.strip() == '5.4.61'
build = output / 'module'
build.mkdir()
for name in ('probe.c', 'dependencies.S', 'Makefile'):
    shutil.copyfile(source / name, build / name)
run([*make, 'M=' + str(build), '-j2', 'modules'])
probe = output / 'cc2_ue300_abi_probe_3000.ko'
shutil.copyfile(build / probe.name, probe)
# Discard build-directory DWARF so the release input has a stable identity.
run([cross + 'strip', '--strip-debug', probe])
undefined = run([cross + 'nm', '-u', probe], capture_output=True, text=True).stdout
symbols = sorted({line.split()[-1] for line in undefined.splitlines()})
expected = set((source / 'kernel-dependencies.txt').read_text().splitlines())
assert expected <= set(symbols)
assert not any('param_ops' in symbol for symbol in symbols)
modinfo = run([cross + 'readelf', '-p', '.modinfo', probe], capture_output=True, text=True).stdout
assert 'vermagic=5.4.61 ' in modinfo and 'license=GPL' in modinfo
header = run([cross + 'readelf', '-h', probe], capture_output=True, text=True).stdout
assert 'ELF32' in header and 'ARM' in header
(output / 'undefined.txt').write_text(undefined)
(output / 'modinfo.txt').write_text(modinfo)
report = {
    'sha256': hashlib.sha256(probe.read_bytes()).hexdigest(),
    'kernel_source': inputs[0][2], 'toolchain_source': inputs[1][2],
    'compiler': 'GCC 6.4.1', 'expected_revision': '3000',
    'source_kernel_suffix_differs': True,
    'layout_checks_preserved': True, 'dependency_symbols': symbols,
    'module_loaded': False, 'physical_qualification': False,
}
(output / 'probe-validation.json').write_text(json.dumps(report, indent=2) + '\n')
print('PROBE BUILD PASSED. No module loaded; physical qualification remains separate.', flush=True)
