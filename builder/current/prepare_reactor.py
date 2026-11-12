#!/usr/bin/env python3
"""Compile the OTA 02.01.01.07 O0 bridge using the qualified ARM toolchain."""
import argparse
import hashlib
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
VENDOR='19712b5943b10bb8cb6aab9a4ed498ee962b28538b23c91b7ed003eb9e590745'
LIBCO='14288a4a73c339b039dd89597618ebb05702f90d668b76f74019d5d15ce67403'
VENDOR13='768a9e8c307d2398dc486be3a3ca8dfa6e9265677627d133f03b6914a1a34e59'
def digest(path):return hashlib.sha256(Path(path).read_bytes()).hexdigest()
def run(args,**kwargs):return subprocess.run([str(x) for x in args],check=True,**kwargs)
def output(args):return run(args,stdout=subprocess.PIPE,text=True).stdout


def verify_arm(path):
    data=path.read_bytes()
    if (len(data)<52 or data[:6]!=b'\x7fELF\x01\x01'
            or struct.unpack_from('<H',data,18)[0]!=40
            or not struct.unpack_from('<I',data,36)[0]&0x400):
        raise RuntimeError('Requires ARM32 hard-float ELF: '+path.name)
    if '.ARM.exidx' not in output([PREFIX+'-readelf','-SW',path]):
        raise RuntimeError('Missing unwind tables: '+path.name)
    versions=output([PREFIX+'-readelf','--version-info',path])
    for name,maximum in [('GLIBC',(2,23)),('GLIBCXX',(3,4,22))]:
        for version in re.findall(r'\b'+name+r'_([0-9.]+)',versions):
            if tuple(map(int,version.split('.')))>maximum:
                raise RuntimeError('Runtime dependency too new: '+version)


def verify_bridge_exports(module):
    symbols=output([PREFIX+'-readelf','--wide','--symbols',module])
    names={line.split()[-1] for line in symbols.splitlines() if line.split()}
    if not {'cc2_callback','cc2_timers','cc2_poll','cc2_wait'}<=names:
        raise RuntimeError('Missing bridge entry points')


def prepare_stock13_component(destination, vendor_libco, flags):
    if destination.exists() or destination.is_symlink():
        raise RuntimeError('Trial output already exists')
    if vendor_libco.is_symlink() or digest(vendor_libco)!=LIBCO:
        raise RuntimeError('Unqualified .13 vendor libco')
    if vendor_libco.name!='libcolib.so':
        raise RuntimeError('The vendor library must be named libcolib.so')
    reactor=SOURCE/'reactor'
    launchers={
        SOURCE/'launcher.sh': 'a260865964759898cf47ed8e30e20eb8981506c1a22ada32611b9911b36b47fc',
        SOURCE/'preflight.sh': '27c3a5bfad9953ac2c64af39a86fd048c6affdd88141a5ea6d76e64b8bee05b2',
        COMPONENT/'firmware-launcher.sh': '8d4a19c9be1f6f85fe5ab9d3f116aa84400e7e6a77c455e66da636373fdaac43',
    }
    with tempfile.TemporaryDirectory(prefix='cc2-reactor13-') as scratch:
        root=Path(scratch)
        prepared=root/'prepared'
        prepared.mkdir()
        module=prepared/'libcc2-reactor-v2.so'
        bridge=prepared/'cc2-reactor-bridge-test'
        run([PREFIX+'-g++',*flags,'-DCC2_REACTOR_STOCK13','-fPIC','-fvisibility=hidden',
             '-shared','-Wl,--no-undefined','-Wl,-z,now',reactor/'reactor_module.cpp',
             '-o',module,'-ldl','-lm'])
        run([PREFIX+'-g++',*flags,'-DCC2_NATIVE_CO','-I'+str(reactor),reactor/'bridge_test.cpp',
             '-o',bridge,'-L'+str(vendor_libco.parent.resolve()),'-Wl,-rpath,/opt/lib',
             '-lcolib','-ldl','-lm'])
        for path in (module,bridge):
            verify_arm(path)
            path.chmod(0o700)
        verify_bridge_exports(module)
        for source,pin in launchers.items():
            if digest(source)!=pin:
                raise RuntimeError('Unqualified Reactor launcher template: '+source.name)
            data=source.read_bytes()
            target=(root if source.name=='firmware-launcher.sh' else prepared)/source.name
            if source.name!='preflight.sh':
                before=b'EXPECTED='+VENDOR.encode()+b'\n'
                if data.count(before)!=1:
                    raise RuntimeError('Unexpected Reactor launcher identity field')
                data=data.replace(before,b'EXPECTED='+VENDOR13.encode()+b'\n')
            target.write_bytes(data)
            target.chmod(0o700)
            run(['sh','-n',target])
        files={p.name:digest(p) for p in sorted(prepared.iterdir())}
        sums=prepared/'SHA256SUMS'
        sums.write_text(''.join(value+'  '+name+'\n' for name,value in files.items()))
        files[sums.name]=digest(sums)
        sources=('reactor_module.cpp','bridge_test.cpp','co_routine.h','fingerprints.h','stock13_profile.h')
        manifest={'optimization':'O0','gcc':'6.5.0','glibc':'2.23','vendor_sha256':VENDOR13,
                  'libco_sha256':LIBCO,'sources':{name:digest(reactor/name) for name in sources},
                  'files':files,'launcher_sha256':digest(root/'firmware-launcher.sh')}
        (root/'prepared-manifest.json').write_text(json.dumps(manifest,indent=2)+'\n')
        shutil.copytree(root,destination)
    print('PASS: .13 Reactor component prepared at',destination)

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    trial=parser.add_mutually_exclusive_group()
    trial.add_argument('--stock13-module',type=Path,metavar='OUTPUT',
                        help='Compile a standalone .13 trial module without packaging or enabling it')
    trial.add_argument('--stock13-component',type=Path,metavar='OUTPUT',
                       help='Prepare the local .13 module, bridge and launchers')
    parser.add_argument('--stock13-libco',type=Path,
                        help='Exact stock .13 libcolib.so input for the component build')
    args=parser.parse_args()
    if bool(args.stock13_component)!=bool(args.stock13_libco):
        parser.error('--stock13-component and --stock13-libco must be used together')
    if output([PREFIX+'-g++','-dumpversion']).strip()!='6.5.0':raise RuntimeError('Requires GCC 6.5.0')
    if args.stock13_module is None and args.stock13_component is None:
        run([sys.executable,SOURCE/'assemble_dual44_gnu.py','--prefix',PREFIX])
        # Both builder preflight paths must use the same qualified wrapper.
        shutil.copyfile(SOURCE/'dual_verify_44.bin',HERE/'dualtrust/dual_verify.bin')
    libc=output([PREFIX+'-g++','-print-file-name=libc.so.6']).strip()
    if 'release version 2.23' not in output(['strings',libc]):raise RuntimeError('Requires glibc 2.23')
    vendor=SOURCE/'reactor/vendor-link/libcolib.so'
    flags=['-std=c++11','-O0','-g','-Wall','-Wextra','-mcpu=cortex-a7','-mfpu=neon-vfpv4','-mfloat-abi=hard','-marm','-pthread','-fno-strict-aliasing','-fno-omit-frame-pointer','-fexceptions','-funwind-tables']
    if args.stock13_component is not None:
        prepare_stock13_component(args.stock13_component,args.stock13_libco,flags)
        return
    if args.stock13_module is not None:
        if args.stock13_module.exists() or args.stock13_module.is_symlink():
            raise RuntimeError('Trial output already exists')
        run([PREFIX+'-g++',*flags,'-DCC2_REACTOR_STOCK13','-fPIC','-fvisibility=hidden',
             '-shared','-Wl,--no-undefined','-Wl,-z,now',SOURCE/'reactor/reactor_module.cpp',
             '-o',args.stock13_module,'-ldl','-lm'])
        print('REACTOR .13 TRIAL MODULE BUILT: launcher integration and hardware tests pending')
        return
    if not vendor.is_file() or digest(vendor)!=LIBCO:raise RuntimeError('Missing qualified native libco')
    prepared=COMPONENT/'prepared';prepared.mkdir(parents=True,exist_ok=True)
    reactor=SOURCE/'reactor';module=prepared/'libcc2-reactor-v2.so';bridge=prepared/'cc2-reactor-bridge-test'
    run([PREFIX+'-g++',*flags,'-fPIC','-fvisibility=hidden','-shared','-Wl,--no-undefined','-Wl,-z,now',reactor/'reactor_module.cpp','-o',module,'-ldl','-lm'])
    run([PREFIX+'-g++',*flags,'-DCC2_NATIVE_CO','-I'+str(reactor),reactor/'bridge_test.cpp','-o',bridge,'-L'+str(vendor.parent),'-Wl,-rpath,/opt/lib','-lcolib','-ldl','-lm'])
    for path in (module,bridge):
        verify_arm(path)
    verify_bridge_exports(module)
    for name in ('launcher.sh','preflight.sh'):
        (prepared/name).write_bytes((SOURCE/name).read_bytes());(prepared/name).chmod(0o700)
    files={name:digest(prepared/name) for name in ('libcc2-reactor-v2.so','cc2-reactor-bridge-test','launcher.sh','preflight.sh')}
    (prepared/'SHA256SUMS').write_text(''.join(value+'  '+name+'\n' for name,value in sorted(files.items())))
    files['SHA256SUMS']=digest(prepared/'SHA256SUMS')
    manifest={'optimization':'O0','gcc':'6.5.0','glibc':'2.23','vendor_sha256':VENDOR,'libco_sha256':LIBCO,'sources':{name:digest(reactor/name) for name in ('reactor_module.cpp','bridge_test.cpp','co_routine.h','fingerprints.h')},'files':files}
    (COMPONENT/'prepared-manifest.json').write_text(json.dumps(manifest,indent=2)+'\n')
    print('REACTOR BUILD PASS: hardware integration remains experimental')
if __name__=='__main__':main()
