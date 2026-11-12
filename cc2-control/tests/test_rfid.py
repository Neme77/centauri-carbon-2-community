"""Real API and process state machine with private fixture files; no printer."""
from pathlib import Path
import hashlib,json,os,shutil,subprocess,tempfile
ROOT=Path(__file__).resolve().parents[1]
def run(args):
    subprocess.run(list(map(str,args)),check=True,timeout=180)
if os.name!='posix':
    print('SKIP: RFID API qualification requires a Linux host')
else:
    with tempfile.TemporaryDirectory(prefix='rfid-api-') as tmp:
        root=Path(tmp);helper=root/'fake-helper';source=root/'helper.c'
        source.write_text('#include <stdio.h>\n#include "rfid_fixture.h"\nint main(void){puts("SCAN ARMED: fixture");puts(tag_fixture);return 0;}\n')
        run(['cc','-O2','-Wall','-Wextra','-Werror','-I',ROOT/'tests',source,'-o',helper]);helper.chmod(0o700)
        digest=hashlib.sha256(helper.read_bytes()).hexdigest()
        flags=['-O2','-UNDEBUG','-std=c11','-Wall','-Wextra','-Wpedantic','-Werror','-D_POSIX_C_SOURCE=200809L','-pthread',
               '-DCC2_RFID_OWNER_UID=geteuid()',f'-DCC2_RFID_HELPER={json.dumps(str(helper))}',
               f'-DCC2_RFID_HELPER_SHA={json.dumps(digest)}',f'-DCC2_RFID_READY={json.dumps(str(root/"ready"))}']
        sources=[ROOT/'src'/n for n in ['mqtt.c','console.c','control.c','panda.c','uds.c','network_status.c']]
        builds=[('cc',[],[])]
        if os.environ.get('CC2_TEST_ARM')=='1':builds.append(('arm-linux-gnueabihf-gcc',['-static'],['qemu-arm']))
        if os.environ.get('CC2_TEST_SANITIZERS')=='1':builds.append(('cc',['-fsanitize=address,undefined','-fno-sanitize-recover=all'],[]))
        for i,(compiler,extra,runner) in enumerate(builds):
            binary=root/('api-'+str(i))
            run([compiler,*flags,*extra,ROOT/'tests/test_rfid_api.c',*sources,'-lm','-o',binary]);run([*runner,binary])
    print('PASS: RFID owned preview, protected apply, native-state and deferred preflight cancellation regressions')
