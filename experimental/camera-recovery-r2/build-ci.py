import hashlib,json,os,re,subprocess,shutil,sys
from pathlib import Path
root=Path(__file__).resolve().parent
os.chdir(root)
out=root/'output';out.mkdir(exist_ok=True)
def run(args,**kw):subprocess.run(list(map(str,args)),check=True,timeout=180,**kw)
flags=['-O2','-UNDEBUG','-std=c11','-Wall','-Wextra','-Wpedantic','-Werror','-pthread']
sources=['test_recovery.c','recovery_core.c','sha256_file.c']
run(['gcc',*flags,*sources,'-o',out/'test-recovery'])
run([out/'test-recovery'])
run(['gcc',*flags,'-fsanitize=address,undefined','-fno-omit-frame-pointer',*sources,'-o',out/'test-recovery-asan'])
run([out/'test-recovery-asan'],env=dict(os.environ,ASAN_OPTIONS='detect_leaks=1:abort_on_error=1'))
arm=[os.environ['CC2_ZIG'],'cc','-target','arm-linux-gnueabihf.2.23','-mcpu=cortex_a7','-marm',*flags]
lib=out/'libcc2-camera-recovery-r2.so'
run([*arm,'-fPIC','-shared','camera_recovery.c','recovery_core.c','sha256_file.c','-o',lib])
run([*arm,*sources,'-o',out/'test-recovery-arm'])
run(['qemu-arm','-L','/usr/arm-linux-gnueabihf',out/'test-recovery-arm'])
run(['arm-linux-gnueabihf-gcc',*flags,'-c','abi_call.c','-o',out/'abi-call-arm.o'])
run(['arm-linux-gnueabihf-g++','-O2','-UNDEBUG','-std=c++17','-Wall','-Wextra','-Werror','test_abi.cpp',out/'abi-call-arm.o','-o',out/'test-abi-arm'])
run(['qemu-arm','-L','/usr/arm-linux-gnueabihf',out/'test-abi-arm'])
run(['qemu-arm','-L','/usr/arm-linux-gnueabihf','-E','CC2_CAMERA_RECOVERY_TRIAL=0','-E',f'LD_PRELOAD={lib}',out/'test-abi-arm'])
wrong=subprocess.run(['qemu-arm','-L','/usr/arm-linux-gnueabihf','-E','CC2_CAMERA_RECOVERY_TRIAL=1','-E',f'LD_PRELOAD={lib}',out/'test-abi-arm'],capture_output=True,text=True,timeout=10)
assert wrong.returncode==126 and 'executable path' in wrong.stderr,wrong
versions=subprocess.check_output(['arm-linux-gnueabihf-readelf','-V',lib],text=True)
required=[tuple(map(int,x.split('.'))) for x in re.findall(r'GLIBC_(\d+\.\d+(?:\.\d+)?)',versions)]
assert required and max(required)<=(2,23),versions
run(['sh','-n','trial.sh'])
run([sys.executable,'test_trial.py'])
result={'recovery_library_sha256':hashlib.sha256(lib.read_bytes()).hexdigest(),
 'glibc_max_required':'.'.join(map(str,max(required))), 'host_and_arm_socket_tests':True,
 'concurrent_ordering_cases_each_run':200,'asan_ubsan':True,'arm_libstdcxx_layout':True,
 'inert_preload_and_wrong_executable_refusal':True,'direct_startup_regression_test':True,
 'lan_enable_once':True,'native_publication_hook_hardware_qualified':False,
 'matrix_recovery_hardware_qualified':False,'persistent_installation':False}
(out/'validation.json').write_text(json.dumps(result,indent=2)+'\n')
(out/'required-symbol-versions.txt').write_text(versions)
for name in ('trial.sh','README.it.md','README.en.md'):shutil.copyfile(root/name,out/name)
print(json.dumps(result,indent=2))
