"""Strict host guard and native loader, using dummy executables/libraries only."""
from pathlib import Path
import hashlib
import json
import os
import re
import shutil
import subprocess
import tempfile

HERE = Path(__file__).resolve().parent
HOST = HERE.parent / 'host'
sha = lambda p: hashlib.sha256(p.read_bytes()).hexdigest()
# Shared by the static callback fixtures and every dynamic module/helper build.
# Current cross-toolchains default to time64; the qualified printer ABI is 32.
LEGACY_ARM_FLAGS = ('-marm', '-mcpu=cortex-a7', '-mfpu=neon-vfpv4', '-mfloat-abi=hard',
                    '-D_TIME_BITS=32', '-D_FILE_OFFSET_BITS=32')

def run():
    flags = ['-O2', '-std=c11', '-Wall', '-Wextra', '-Wpedantic', '-Werror',
             '-D_POSIX_C_SOURCE=200809L', '-pthread', '-funwind-tables', '-fno-strict-aliasing']
    arm = list(LEGACY_ARM_FLAGS)
    library = Path('/output/libcc2-rfid.so')
    subprocess.run(['arm-linux-gnueabihf-gcc', *flags, *arm, '-fPIC', '-fvisibility=hidden', '-shared',
                    '-Wl,-z,defs', '-Wl,-z,relro,-z,now', '-DRF_REQUIRED_NATIVE=1',
                    '-DRF_COMPONENT="/opt/usr/cc2-rfid"', HOST/'native_module.c', HOST/'rfid_guard.c',
                    '-o', library], check=True, timeout=120)
    versions = subprocess.check_output(['arm-linux-gnueabihf-readelf', '-V', library], text=True)
    required = [tuple(map(int, v.split('.'))) for v in re.findall(r'GLIBC_([0-9.]+)', versions)]
    assert required and max(required) <= (2, 23), versions
    dynamic = subprocess.check_output(['arm-linux-gnueabihf-readelf', '-d', library], text=True)
    assert 'TEXTREL' not in dynamic and 'libstdc++' not in dynamic
    count = 0
    with tempfile.TemporaryDirectory(prefix='cc2-required-rfid-') as temporary:
        root = Path(temporary)
        stock = root/'stock-lib'; stock.mkdir()
        stock_pins = {
            'ld-2.23.so': '05b356166c83835d470fcc751cdff8062c5322a3a6e7c271f94f604f0425c39b',
            'libc-2.23.so': 'af176e5c3aabe81f293d574727d47060a5d49c452f87afce724c2968ae432a2a',
            'libgcc_s.so.1': '75380dfa9bfee280dc0f82a35114241a31205380b06027c796df617635cacf18',
            'libpthread-2.23.so': '3374386b829d26645f0e25c171bd3a1d5c56ae6d59083fd2bed194ed5a926610',
        }
        for name, digest in stock_pins.items():
            source = Path('/stock-libs')/name
            assert sha(source) == digest
            shutil.copyfile(source, stock/name)
        (stock/'ld-2.23.so').chmod(0o755)
        for source, name in [('ld-2.23.so','ld-linux-armhf.so.3'), ('libc-2.23.so','libc.so.6'),
                             ('libpthread-2.23.so','libpthread.so.0')]: (stock/name).symlink_to(source)
        dummy = root/'dummy'
        (root/'dummy.c').write_text('int main(void){return 0;}\n')
        subprocess.run(['arm-linux-gnueabihf-gcc', *arm, root/'dummy.c', HOST/'test_stock_start.c',
                        '-Wl,--wrap=__libc_start_main', '-o', dummy], check=True)
        # Only the test library wraps readlink: select the qualified native path
        # without executing the vendor binary or touching its code addresses.
        wrapper = root/'wrap.c'
        wrapper.write_text('''#include <unistd.h>
#include <stdlib.h>
#include <string.h>
ssize_t __real_readlink(const char *,char *,size_t);
ssize_t __wrap_readlink(const char *path,char *out,size_t size) {
 const char *native="/opt/usr/cc2-reactor-runtime-v2/elegoo_printer";
 if(getenv("RF_TEST_NATIVE_PATH") && !strcmp(path,"/proc/self/exe")) {
  size_t n=strlen(native);if(n>size)n=size;memcpy(out,native,n);return (ssize_t)n;
 }
 return __real_readlink(path,out,size);
}
''')
        component = root/'component'
        for required_mode in [0, 1]:
            test_library = root/f'guard-{required_mode}.so'
            subprocess.run(['arm-linux-gnueabihf-gcc', *flags, *arm, '-fPIC', '-shared', '-Wl,-z,defs',
                            '-Wl,--wrap=readlink', f'-DRF_REQUIRED_NATIVE={required_mode}',
                            '-DRF_COMPONENT='+json.dumps(str(component)), HOST/'native_module.c',
                            HOST/'rfid_guard.c', wrapper, '-o', test_library], check=True, timeout=120)
            for case in ['absent-component', 'absent-marker', 'wrong-marker', 'symlink-marker', 'public-marker', 'valid-marker-wrong-exe', 'foreign-helper']:
                if component.exists():
                    # Newly created test fixtures only, inside this TemporaryDirectory.
                    for p in component.iterdir(): p.unlink()
                    component.rmdir()
                if case != 'absent-component': component.mkdir(mode=0o700)
                if case not in ['absent-component','absent-marker']:
                    marker = component/'enabled'
                    marker.write_bytes(b'wrong' if case == 'wrong-marker' else b'CC2-RFID-NATIVE-R2\n')
                    marker.chmod(0o644 if case == 'public-marker' else 0o600)
                    if case == 'symlink-marker':
                        marker.rename(component/'target'); marker.symlink_to('target')
                for runtime in ['current', 'stock']:
                    args = ['qemu-arm', '-L', '/usr/arm-linux-gnueabihf', '-E', f'LD_PRELOAD={test_library}']
                    if case != 'foreign-helper': args += ['-E', 'RF_TEST_NATIVE_PATH=1']
                    args += [dummy] if runtime == 'current' else [stock/'ld-2.23.so', '--library-path', stock, dummy]
                    result = subprocess.run(args, capture_output=True, text=True, timeout=10)
                    expected = 0 if case == 'foreign-helper' or (not required_mode and case in ['absent-component','absent-marker']) else 125
                    assert result.returncode == expected, (required_mode, case, runtime, result.returncode, result.stdout, result.stderr)
                    count += 1
        # Production strict module must leave an unrelated helper untouched.
        result = subprocess.run(['qemu-arm', '-E', f'LD_PRELOAD={library}', stock/'ld-2.23.so',
                                 '--library-path', stock, dummy], capture_output=True, text=True, timeout=10)
        assert result.returncode == 0, (result.stdout, result.stderr)

        tiny = root/'tiny.c'
        tiny.write_text('void fixture_only(void){}\n')
        fake_library = root/'fixture.so'
        subprocess.run(['gcc','-fPIC','-shared',tiny,'-o',fake_library], check=True)
        native = root/'native'
        (root/'native.c').write_text('''#include <stdio.h>
#include <stdlib.h>
int main(int argc,char **argv){
 printf("NATIVE:%s:%s:%s:%s:%s\\n",getenv("CC2_REACTOR_RUNTIME_V2"),getenv("CC2_NETWORK_COMPAT_TRIAL"),getenv("LD_PRELOAD"),argc>1?argv[1]:"",argc>2?argv[2]:"");return 0;
}
''')
        subprocess.run(['gcc','-Wall','-Wextra','-Werror',root/'native.c','-o',native], check=True)
        for shell in [['dash'],['busybox','sh']]:
            for case in ['wifi','ethernet','network-ensure-fails','no-network','probe-fails','manifest-changed','target-changed','missing-marker']:
                folder = root/f'{shell[0]}-{case}'; folder.mkdir(mode=0o700)
                checker = folder/'checker'
                checker.write_text('''#!/bin/sh
[ "$CC2_REACTOR_RUNTIME_V2:$CC2_NETWORK_COMPAT_TRIAL:$CC2_CAMERA_REAP_TRIAL:$CC2_CAMERA_RECOVERY_TRIAL" = 0:0:0:0 ] || exit 4
[ "$TEST_CASE" != probe-fails ]
'''); checker.chmod(0o700)
                control = folder/'network-control'
                control.write_text('''#!/bin/sh
case "$1" in
 --enabled) [ "$TEST_CASE" = ethernet ] || [ "$TEST_CASE" = network-ensure-fails ];;
 --ensure) [ "$TEST_CASE" != network-ensure-fails ];;
 *) exit 2;;
esac
'''); control.chmod(0o700)
                marker = folder/'enabled'; marker.write_text('fixture-marker\n'); marker.chmod(0o600)
                library_copy = folder/'guard.so'; shutil.copyfile(fake_library, library_copy)
                manifest = folder/'guarded.sha256'
                manifest.write_text(''.join(f'{sha(p)}  {p}\n' for p in [native,checker,control,library_copy,marker]))
                source = (HERE/'guarded-entry.sh').read_text()
                replacements = {
                    'NATIVE=/opt/usr/cc2-reactor-runtime-v2/elegoo_printer': f'NATIVE={native}',
                    'MANIFEST=/opt/usr/cc2-rfid/guarded.sha256': f'MANIFEST={manifest}',
                    'MANIFEST_SHA=UNCONFIGURED': f'MANIFEST_SHA={sha(manifest)}',
                    'CHECKER=/opt/usr/cc2-control/camera/cc2-camera-reap-check': f'CHECKER={checker}',
                    'CORE_PRELOADS=/opt/usr/cc2-reactor-runtime-v2/libcc2-reactor-v2.so:/opt/usr/cc2-rfid/libcc2-rfid.so': f'CORE_PRELOADS={library_copy}',
                    'NETWORK_CONTROL=\n': f'NETWORK_CONTROL={control if case != "no-network" else ""}\n',
                    'NETWORK_LIBRARY=\n': f'NETWORK_LIBRARY={library_copy if case != "no-network" else ""}\n',
                    'LIBRARY_PATH=/opt/lib': f'LIBRARY_PATH={folder}',
                }
                for old,new in replacements.items():
                    assert source.count(old)==1,old
                    source=source.replace(old,new)
                entry=folder/'entry';entry.write_text(source)
                if case=='manifest-changed': manifest.write_text(manifest.read_text()+'changed\n')
                if case=='target-changed': library_copy.write_bytes(b'changed')
                if case=='missing-marker': marker.unlink()
                result=subprocess.run([*shell,entry,'printer.cfg','-s'],env=dict(os.environ,TEST_CASE=case),capture_output=True,text=True,timeout=10)
                okay=case in ['wifi','ethernet','network-ensure-fails','no-network']
                assert (result.returncode==0)==okay,(shell,case,result.returncode,result.stdout,result.stderr)
                if okay:
                    network=case in ['ethernet','network-ensure-fails']
                    preloads=f'{library_copy}:{library_copy}' if network else str(library_copy)
                    assert f'NATIVE:1:{int(network)}:{preloads}:printer.cfg:-s' in result.stdout
                else: assert 'NATIVE:' not in result.stdout
                count+=1
    print(f'PASS: {count} strict/legacy constructor and guarded-entry cases; helpers inert, stock GLIBC 2.23, Wi-Fi/LAN flags and no refusal fallback.',flush=True)
    return {'cases':count,'library_sha256':sha(library),'maximum_glibc':'.'.join(map(str,max(required)))}

if __name__=='__main__':run()
