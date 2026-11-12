"""Execute actual loader scripts with dummy native ELF and a rejecting preload."""
from pathlib import Path
import hashlib,json,os,subprocess,tempfile,zipfile
from prepare_payload import add_floor,qualified_network_entry,prepare

repo=Path(__file__).resolve().parents[2]
def sha(data):return hashlib.sha256(data).hexdigest()
def run(args,**kw):return subprocess.run(list(map(str,args)),check=True,timeout=30,**kw)
with tempfile.TemporaryDirectory(prefix='cavity-boot-') as temporary:
    root=Path(temporary);runtime=root/'runtime';network=root/'network';floor=root/'floor'
    for directory in [runtime,network,floor]:directory.mkdir()
    native=runtime/'elegoo_printer'
    (root/'native.c').write_text('#include <stdio.h>\n#include <stdlib.h>\nint main(void){puts(getenv("LD_PRELOAD")?getenv("LD_PRELOAD"):"");return 0;}\n')
    run(['gcc','-O2','-Wall','-Wextra','-Werror',root/'native.c','-o',native])
    guard='#define _GNU_SOURCE\n#include <unistd.h>\n#include <string.h>\n__attribute__((constructor))static void guard(void){char p[1024];ssize_t n=readlink("/proc/self/exe",p,sizeof(p)-1);if(n<0)_exit(125);p[n]=0;if(strcmp(p,'+json.dumps(str(native))+')){ssize_t written=write(2,"REJECTED_FOREIGN_HELPER\\n",24);if(written!=24)_exit(124);_exit(125);}}\n'
    (root/'guard.c').write_text(guard)
    reactor=runtime/'libcc2-reactor-v2.so';module=floor/'libcc2-cavity-floor-r1.so';compat=network/'libcc2-network-compat-r1.so'
    run(['gcc','-O2','-Wall','-Wextra','-Werror','-fPIC','-shared',root/'guard.c','-o',reactor])
    (root/'empty.c').write_text('void fixture_library(void){}\n')
    run(['gcc','-O2','-Wall','-Wextra','-Werror','-fPIC','-shared',root/'empty.c','-o',module])
    compat.write_bytes(module.read_bytes())
    module_bytes=module.read_bytes();module_sha=sha(module_bytes)
    control=network/'control'
    control.write_text('#!/bin/sh\n[ -z "$LD_PRELOAD" ] || exit 99\ncase "$1" in --enabled) [ "$NETWORK_ENABLED" = 1 ];; --ensure) exit 0;; *) exit 1;;esac\n')
    control.chmod(0o700)
    original=(repo/'experimental/firmware-port/launcher.sh').read_bytes()
    sources={'runtime':add_floor(original,module_sha),'network':add_floor(qualified_network_entry(original),module_sha)}
    def adapt(data):
        return data.decode().replace('/opt/usr/cc2-reactor-runtime-v2',str(runtime)).replace('/opt/usr/cc2-network-manager',str(network)).replace('/opt/usr/cc2-cavity-floor',str(floor)).replace('19712b5943b10bb8cb6aab9a4ed498ee962b28538b23c91b7ed003eb9e590745',sha(native.read_bytes())).replace('52d400605dd51f89eb95248c42a1661b3c0e60fadec5453d27a987729332fb10',module_sha)
    def execute(shell,text,enabled,net):
        launcher=runtime/'launcher.sh';launcher.write_text(text)
        (runtime/'SHA256SUMS').write_text(''.join(sha(file.read_bytes())+'  '+file.name+'\n' for file in [native,reactor,launcher]))
        marker=floor/'enabled'
        if enabled:marker.write_text('CC2-CAVITY-FLOOR-R1\n')
        elif marker.exists():marker.unlink()
        env={k:v for k,v in os.environ.items() if k!='LD_PRELOAD'}
        env['NETWORK_ENABLED']=str(net)
        return run([*shell,launcher,'dummy-config','-s','dummy-autosave'],capture_output=True,text=True,env=env)
    archive=Path(os.environ.get('CC2_LEGACY_TRIAL_ZIP','/input/qualified/CC2-Control-Exhaust-Minimum-Trial-R1b.zip'))
    with zipfile.ZipFile(archive) as z:legacy=z.read(archive.stem+'/cc2-cavity-floor/launcher.sh')
    broken=execute(['dash'],adapt(legacy),True,0)
    assert 'REJECTED_FOREIGN_HELPER' in broken.stderr and str(module) not in broken.stdout
    bypass=execute(['dash'],adapt(qualified_network_entry(original)),True,1)
    assert str(module) not in bypass.stdout
    print('REPRODUCED: shipped runtime loader exports reactor before cat/checksum helpers; original network delegate bypasses the optional module.',flush=True)
    count=0
    for shell in [['dash'],['busybox','sh']]:
        for name,data in sources.items():
            for net in [0,1]:
                for state in ['enabled','disabled','missing-library','damaged-library']:
                    module.write_bytes(module_bytes)
                    if state=='missing-library':module.unlink()
                    if state=='damaged-library':module.write_bytes(module_bytes+b'changed')
                    result=execute(shell,adapt(data),state!='disabled',net)
                    assert 'REJECTED_FOREIGN_HELPER' not in result.stderr,(shell,name,net,state,result.stderr)
                    assert (str(module) in result.stdout)==(state=='enabled'),(shell,name,net,state,result.stdout)
                    assert (str(compat) in result.stdout)==(name=='network' and net==1)
                    count+=1
    assert count==32
    module.write_bytes(module_bytes)
    built=prepare(repo,module,root/'payload')
    for name in ['launcher.sh','reactor-entry.sh','activate.sh']:run(['dash','-n',built/name])
    print('PASS: 32 actual loader executions on dash/BusyBox with guarded dummy ELF, LAN on/off, disabled/missing/damaged addon; helpers never inherit reactor preload. No vendor executable run.',flush=True)
