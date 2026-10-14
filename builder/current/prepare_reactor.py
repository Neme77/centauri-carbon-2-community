#!/usr/bin/env python3
"""Compile the OTA 02.01.01.07 O0 bridge using the qualified ARM toolchain."""
import hashlib
import json
from pathlib import Path
import re
import shutil
import subprocess
import struct
import sys

HERE=Path(__file__).resolve().parent
REPO=HERE.parents[1]
SOURCE=REPO/'experimental/firmware-port'
COMPONENT=HERE/'components/reactor'
PREFIX='/opt/cc2-cross/toolchain-out/bin/arm-cortex_a15-linux-gnueabihf'
VENDOR='19712b5943b10bb8cb6aab9a4ed498ee962b28538b23c91b7ed003eb9e590745'
LIBCO='14288a4a73c339b039dd89597618ebb05702f90d668b76f74019d5d15ce67403'
def digest(path):return hashlib.sha256(Path(path).read_bytes()).hexdigest()
def run(args,**kwargs):return subprocess.run([str(x) for x in args],check=True,**kwargs)
def output(args):return run(args,stdout=subprocess.PIPE,text=True).stdout

def main():
    if output([PREFIX+'-g++','-dumpversion']).strip()!='6.5.0':raise RuntimeError('Requires GCC 6.5.0')
    run([sys.executable,SOURCE/'assemble_dual44_gnu.py','--prefix',PREFIX])
    # Both builder preflight paths must use the same newly reproduced, hash-qualified wrapper.
    shutil.copyfile(SOURCE/'dual_verify_44.bin',HERE/'dualtrust/dual_verify.bin')
    libc=output([PREFIX+'-g++','-print-file-name=libc.so.6']).strip()
    if 'release version 2.23' not in output(['strings',libc]):raise RuntimeError('Requires glibc 2.23')
    vendor=SOURCE/'reactor/vendor-link/libcolib.so'
    if not vendor.is_file() or digest(vendor)!=LIBCO:raise RuntimeError('Missing qualified native libco')
    prepared=COMPONENT/'prepared';prepared.mkdir(parents=True,exist_ok=True)
    flags=['-std=c++11','-O0','-g','-Wall','-Wextra','-mcpu=cortex-a7','-mfpu=neon-vfpv4','-mfloat-abi=hard','-marm','-pthread','-fno-strict-aliasing','-fno-omit-frame-pointer','-fexceptions','-funwind-tables']
    reactor=SOURCE/'reactor';module=prepared/'libcc2-reactor-v2.so';bridge=prepared/'cc2-reactor-bridge-test'
    run([PREFIX+'-g++',*flags,'-fPIC','-fvisibility=hidden','-shared','-Wl,--no-undefined','-Wl,-z,now',reactor/'reactor_module.cpp','-o',module,'-ldl','-lm'])
    run([PREFIX+'-g++',*flags,'-DCC2_NATIVE_CO','-I'+str(reactor),reactor/'bridge_test.cpp','-o',bridge,'-L'+str(vendor.parent),'-Wl,-rpath,/opt/lib','-lcolib','-ldl','-lm'])
    for path in (module,bridge):
        data=path.read_bytes()
        if data[:6]!=b'\x7fELF\x01\x01' or struct.unpack_from('<H',data,18)[0]!=40 or not struct.unpack_from('<I',data,36)[0]&0x400:raise RuntimeError('Requires ARM32 hard-float ELF')
        if '.ARM.exidx' not in output([PREFIX+'-readelf','-SW',path]):raise RuntimeError('Missing unwind tables')
        versions=output([PREFIX+'-readelf','--version-info',path])
        for name,maximum in [('GLIBC',(2,23)),('GLIBCXX',(3,4,22))]:
            for version in re.findall(r'\b'+name+r'_([0-9.]+)',versions):
                if tuple(map(int,version.split('.')))>maximum:raise RuntimeError('Runtime dependency too new: '+version)
    symbols=output([PREFIX+'-readelf','--wide','--symbols',module]);names={line.split()[-1] for line in symbols.splitlines() if line.split()}
    if not {'cc2_callback','cc2_timers','cc2_poll','cc2_wait'}<=names:raise RuntimeError('Missing bridge entry points')
    for name in ('launcher.sh','preflight.sh'):
        (prepared/name).write_bytes((SOURCE/name).read_bytes());(prepared/name).chmod(0o700)
    files={name:digest(prepared/name) for name in ('libcc2-reactor-v2.so','cc2-reactor-bridge-test','launcher.sh','preflight.sh')}
    (prepared/'SHA256SUMS').write_text(''.join(value+'  '+name+'\n' for name,value in sorted(files.items())))
    files['SHA256SUMS']=digest(prepared/'SHA256SUMS')
    manifest={'optimization':'O0','gcc':'6.5.0','glibc':'2.23','vendor_sha256':VENDOR,'libco_sha256':LIBCO,'sources':{name:digest(reactor/name) for name in ('reactor_module.cpp','bridge_test.cpp','co_routine.h','fingerprints.h')},'files':files}
    (COMPONENT/'prepared-manifest.json').write_text(json.dumps(manifest,indent=2)+'\n')
    print('REACTOR BUILD PASS: hardware integration remains experimental')
if __name__=='__main__':main()
