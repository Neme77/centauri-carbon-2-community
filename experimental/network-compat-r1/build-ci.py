import hashlib,json,os,re,subprocess,sys
from pathlib import Path
root=Path(__file__).resolve().parent
os.chdir(root)
out=root/'output';out.mkdir(exist_ok=True)
def run(args,**kwargs):
    subprocess.run([str(x) for x in args],check=True,timeout=180,**kwargs)
flags=['-O2','-UNDEBUG','-std=c11','-Wall','-Wextra','-Wpedantic','-Werror','-pthread']
run(['gcc',*flags,'net_core.c','test_core.c','-o',out/'test-core'])
run([out/'test-core'])
run(['gcc',*flags,'-fsanitize=address,undefined','-fno-omit-frame-pointer','net_core.c','test_core.c','-o',out/'test-core-asan'])
run([out/'test-core-asan'])
run(['gcc',*flags,'-c','abi_call.c','-o',out/'abi-call-host.o'])
run(['gcc',*flags,'-c','net_core.c','-o',out/'core-host.o'])
run(['g++','-O2','-UNDEBUG','-std=c++17','-Wall','-Wextra','-Werror','test_abi.cpp',out/'abi-call-host.o',out/'core-host.o','-o',out/'test-abi-host'])
run([out/'test-abi-host'])
for name in ('manage.sh','preflight.sh','thermal_selftest.sh'):
    run(['sh','-n',name])
run(['sh','thermal_selftest.sh'])
run([sys.executable,'test_trial.py'])
arm=[os.environ['CC2_ZIG'],'cc','-target','arm-linux-gnueabihf.2.23','-mcpu=cortex_a7','-marm',*flags]
library=out/'libcc2-network-compat-r1.so'
run([*arm,'-fPIC','-shared','-s','network_compat.c','net_core.c','sha256_file.c','-o',library])
run([*arm,'net_core.c','test_core.c','-o',out/'test-core-arm'])
run(['qemu-arm','-L','/usr/arm-linux-gnueabihf',out/'test-core-arm'])
run(['arm-linux-gnueabihf-gcc',*flags,'-marm','-c','abi_call.c','-o',out/'abi-call-arm.o'])
run(['arm-linux-gnueabihf-gcc',*flags,'-marm','-c','net_core.c','-o',out/'core-arm.o'])
run(['arm-linux-gnueabihf-g++','-O2','-UNDEBUG','-std=c++17','-Wall','-Wextra','-Werror','test_abi.cpp',out/'abi-call-arm.o',out/'core-arm.o','-o',out/'test-abi-arm'])
run(['qemu-arm','-L','/usr/arm-linux-gnueabihf',out/'test-abi-arm'])
run([*arm,'test_hooks_arm.c','net_core.c','-o',out/'test-hooks-arm'])
for role in ('camera','printer'):
    run(['sudo','qemu-arm','-L','/usr/arm-linux-gnueabihf',out/'test-hooks-arm',role])
run(['qemu-arm','-L','/usr/arm-linux-gnueabihf','-E','CC2_NETWORK_COMPAT_TRIAL=0','-E',f'LD_PRELOAD={library}',out/'test-abi-arm'])
wrong=subprocess.run(['qemu-arm','-L','/usr/arm-linux-gnueabihf','-E','CC2_NETWORK_COMPAT_TRIAL=1','-E',f'LD_PRELOAD={library}',out/'test-abi-arm'],capture_output=True,text=True,timeout=20)
assert wrong.returncode==126 and 'unqualified executable' in wrong.stderr,wrong
versions=subprocess.check_output(['arm-linux-gnueabihf-readelf','-V',library],text=True)
required=[tuple(map(int,x.split('.'))) for x in re.findall(r'GLIBC_(\d+\.\d+(?:\.\d+)?)',versions)]
assert required and max(required)<=(2,23),versions
(out/'required-symbol-versions.txt').write_text(versions)
result={'library_sha256':hashlib.sha256(library.read_bytes()).hexdigest(),
        'glibc_max_required':'.'.join(map(str,max(required))),
        'host_core_asan_ubsan':True,'real_string_abi_host_arm':True,
        'live_arm_entry_trampoline_camera_printer':True,'inert_preload_and_foreign_executable_refusal':True,
        'shell_syntax_and_five_restore_scenarios':True,
        'hardware_qualified':False,'automatic_quality_selection':False,'persistent_installation':False}
(out/'validation.json').write_text(json.dumps(result,indent=2)+'\n')
print(json.dumps(result,indent=2))
