#!/usr/bin/env python3
"""Compile the O0 reactor bridge for a qualified base using the ARM toolchain.

Base-aware: --base-version selects the patched-printer pin (from the profile's
port.outputs.printer_release.sha256) and the per-base reactor sources. 07 builds
exactly as before (same paths, byte-identical output). A base whose sources span
directories (e.g. 02.01.01.13: reactor_module.cpp/fingerprints.h in the 0113
tree, bridge_test.cpp/co_routine.h shared with 07) is compiled from a staging
copy so bridge_test.cpp's quote-include of reactor_module.cpp resolves to the
base's version.
"""
import argparse
import hashlib
import importlib.util
import json
from pathlib import Path
import re
import shutil
import subprocess
import struct
import sys
import tempfile

HERE=Path(__file__).resolve().parent
REPO=HERE.parents[1]
SOURCE=REPO/'experimental/firmware-port'
COMPONENT=HERE/'components/reactor'
PREFIX='/opt/cc2-cross/toolchain-out/bin/arm-cortex_a15-linux-gnueabihf'
LIBCO='14288a4a73c339b039dd89597618ebb05702f90d668b76f74019d5d15ce67403'
_spec=importlib.util.spec_from_file_location('reactor_component',HERE/'core/reactor_component.py')
_rc=importlib.util.module_from_spec(_spec);_spec.loader.exec_module(_rc)
reactor_source=_rc.reactor_source
SRC_NAMES=('reactor_module.cpp','bridge_test.cpp','co_routine.h','fingerprints.h')
def digest(path):return hashlib.sha256(Path(path).read_bytes()).hexdigest()
def run(args,**kwargs):return subprocess.run([str(x) for x in args],check=True,**kwargs)
def output(args):return run(args,stdout=subprocess.PIPE,universal_newlines=True).stdout

def main():
    ap=argparse.ArgumentParser(description='Compile the O0 reactor bridge for a base')
    ap.add_argument('--base-version',default='02.01.01.07')
    ap.add_argument('--prefix',default=PREFIX,help='cross toolchain prefix for -g++/-readelf (default: the canonical /opt/cc2-cross path)')
    args=ap.parse_args(); base=args.base_version; prefix=args.prefix
    profile=json.loads((HERE/'base_profiles'/(base+'.json')).read_text())
    vendor_sha=profile['port']['outputs']['printer_release']['sha256']
    if output([prefix+'-g++','-dumpversion']).strip()!='6.5.0':raise RuntimeError('Requires GCC 6.5.0')
    run([sys.executable,SOURCE/'assemble_dual44_gnu.py','--prefix',prefix])
    # Both builder preflight paths must use the same newly reproduced, hash-qualified wrapper.
    shutil.copyfile(str(SOURCE/'dual_verify_44.bin'),str(HERE/'dualtrust/dual_verify.bin'))
    libc=output([prefix+'-g++','-print-file-name=libc.so.6']).strip()
    if 'release version 2.23' not in output(['strings',libc]):raise RuntimeError('Requires glibc 2.23')
    vendor=SOURCE/'reactor/vendor-link/libcolib.so'
    if not vendor.is_file() or digest(vendor)!=LIBCO:raise RuntimeError('Missing qualified native libco')
    prepared=COMPONENT/'prepared';prepared.mkdir(parents=True,exist_ok=True)
    flags=['-std=c++11','-O0','-g','-Wall','-Wextra','-mcpu=cortex-a7','-mfpu=neon-vfpv4','-mfloat-abi=hard','-marm','-pthread','-fno-strict-aliasing','-fno-omit-frame-pointer','-fexceptions','-funwind-tables']
    module=prepared/'libcc2-reactor-v2.so';bridge=prepared/'cc2-reactor-bridge-test'
    sources={name:reactor_source(name,base) for name in SRC_NAMES}
    staging=None
    if len({p.parent for p in sources.values()})==1:
        build=next(iter(sources.values())).parent  # 07: one directory, compile in place (byte-identical)
    else:
        staging=Path(tempfile.mkdtemp(prefix='cc2-reactor-build-'))
        for name,p in sources.items():shutil.copyfile(str(p),str(staging/name))  # co-locate so quote-includes resolve to this base
        build=staging
    try:
        run([prefix+'-g++',*flags,'-fPIC','-fvisibility=hidden','-shared','-Wl,--no-undefined','-Wl,-z,now',build/'reactor_module.cpp','-o',module,'-ldl','-lm'])
        run([prefix+'-g++',*flags,'-DCC2_NATIVE_CO','-I'+str(build),build/'bridge_test.cpp','-o',bridge,'-L'+str(vendor.parent),'-Wl,-rpath,/opt/lib','-lcolib','-ldl','-lm'])
    finally:
        if staging is not None:shutil.rmtree(staging,ignore_errors=True)
    for path in (module,bridge):
        data=path.read_bytes()
        if data[:6]!=b'\x7fELF\x01\x01' or struct.unpack_from('<H',data,18)[0]!=40 or not struct.unpack_from('<I',data,36)[0]&0x400:raise RuntimeError('Requires ARM32 hard-float ELF')
        if '.ARM.exidx' not in output([prefix+'-readelf','-SW',path]):raise RuntimeError('Missing unwind tables')
        versions=output([prefix+'-readelf','--version-info',path])
        for name,maximum in [('GLIBC',(2,23)),('GLIBCXX',(3,4,22))]:
            for version in re.findall(r'\b'+name+r'_([0-9.]+)',versions):
                if tuple(map(int,version.split('.')))>maximum:raise RuntimeError('Runtime dependency too new: '+version)
    symbols=output([prefix+'-readelf','--wide','--symbols',module]);names={line.split()[-1] for line in symbols.splitlines() if line.split()}
    if not {'cc2_callback','cc2_timers','cc2_poll','cc2_wait'}<=names:raise RuntimeError('Missing bridge entry points')
    for name in ('launcher.sh','preflight.sh'):
        (prepared/name).write_bytes(reactor_source(name,base).read_bytes());(prepared/name).chmod(0o700)
    files={name:digest(prepared/name) for name in ('libcc2-reactor-v2.so','cc2-reactor-bridge-test','launcher.sh','preflight.sh')}
    (prepared/'SHA256SUMS').write_text(''.join(value+'  '+name+'\n' for name,value in sorted(files.items())))
    files['SHA256SUMS']=digest(prepared/'SHA256SUMS')
    manifest={'optimization':'O0','gcc':'6.5.0','glibc':'2.23','vendor_sha256':vendor_sha,'libco_sha256':LIBCO,'sources':{name:digest(reactor_source(name,base)) for name in SRC_NAMES},'files':files}
    (COMPONENT/'prepared-manifest.json').write_text(json.dumps(manifest,indent=2)+'\n')
    print('REACTOR BUILD PASS for '+base+': hardware integration remains experimental')
if __name__=='__main__':main()
