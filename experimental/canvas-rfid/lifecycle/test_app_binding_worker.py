"""Configured production worker + actual startup hook + inert native processes.

Only the stock image DATA is real; no vendor program, UART or transfer worker
runs. ARM worker child execution uses a pinned host ELF emulator launcher.
"""
from pathlib import Path
import hashlib,json,os,shutil,struct,subprocess,sys,tempfile,threading,time
from test_stage_worker import fixture,put,spawn,seed_journal,PATHS,MODULES
from test_post_boot import Server,Handler

HERE=Path(__file__).resolve().parent
sha=lambda b:hashlib.sha256(b).hexdigest()
NAMES=['coordinator','capture','app-integration.json','activate-on-ready']
CASES=['installer-dash-success','installer-dash-failure','installer-busybox-success','installer-busybox-failure','upgrade','rollback','rollback-live','rollback-retry','owner-dead','owner-nonempty','recover-without-barrier','partial-recovery','foreign-barrier',
       'wrong-phase','bad-backup','guard-refused','foreign-target','corrupt-descriptor','canvas-unavailable','retire-empty','retire-pending','retire-rolled-back']
FLAGS=['-O2','-std=c11','-Wall','-Wextra','-Wpedantic','-Werror','-D_POSIX_C_SOURCE=200809L']

RECOVERED='APP BINDING RECOVERED: native journal, startup, guard and Canvas unchanged.'

def app_ready(process,context):
    # The real worker reports successful recovery before the app becomes ready.
    # Accept only that exact optional line; still require the app's own READY.
    first=process.stdout.readline().strip()
    line=process.stdout.readline().strip() if first==RECOVERED else first
    assert line=='READY',(context,first,line)

def start_app(path,context):
    process=subprocess.Popen([path],stdout=subprocess.PIPE,stderr=subprocess.PIPE,text=True)
    try:
        app_ready(process,context)
        return process
    except BaseException:
        if process.poll() is None:process.terminate()
        process.wait(timeout=5)
        raise

def plan_digest(old_app,new_app,profile,old,new):
    b=f'CC2RFUP1 {old_app} {new_app} {profile}\n'
    for i,n in enumerate(NAMES):b+=f'{n} {0o700 if i<2 else 0o600:04o} {sha(old[n])} {sha(new[n])}\n'
    return sha(b.encode())

def run(installer_only=False):
    cases=[case for case in CASES if case.startswith('installer-')] if installer_only else CASES
    stock=Path('/stock-canvas.bin').read_bytes();assert sha(stock)=='f1267ee3219dcad73efffa8dc38e30ff0f17c02c0a1674cbead48a99bdff3228'
    count=0
    with tempfile.TemporaryDirectory(prefix='cc2-binding-worker-') as temporary:
        root=Path(temporary);dummy=root/'dummy';dummy_source=root/'dummy.c'
        dummy_source.write_text('#include <fcntl.h>\n#include <stdio.h>\n#include <sys/mman.h>\n#include <unistd.h>\nint main(int n,char**v){for(int i=1;i<n;i++){int f=open(v[i],O_RDONLY);if(f<0||mmap(0,4096,PROT_READ,MAP_PRIVATE,f,0)==MAP_FAILED)return 2;close(f);}puts("READY");fflush(stdout);for(;;)pause();}\n')
        subprocess.run(['gcc',*FLAGS,dummy_source,'-o',dummy],check=True,timeout=60);old_app=dummy.read_bytes()
        with Server(('127.0.0.1',0),Handler) as server:
            server.mode='host';thread=threading.Thread(target=server.serve_forever,daemon=True);thread.start()
            try:
                for arch,compiler,extra in [('host','gcc',[]),('arm','arm-linux-gnueabihf-gcc',['-static','-D_TIME_BITS=32','-D_FILE_OFFSET_BITS=32'])]:
                    folder=root/arch;updates=folder/'updates';barrier=folder/'install.lock'
                    component,tree,native,app,olds,news,profile,fresh=fixture(folder,old_app,stock)
                    new_source=root/(arch+'-app.c');new_binary=root/(arch+'-app')
                    new_source.write_text('#define _GNU_SOURCE\n#include <stdio.h>\n#include "rfid_app_update_boot.h"\nint main(void){struct rlimit lim={1024,1024};if(setrlimit(RLIMIT_NOFILE,&lim)||!rfid_app_update_boot())return 1;puts("READY");fflush(stdout);for(;;)pause();}\n')
                    subprocess.run(['gcc',*FLAGS,'-DCC2_RFID_APP_UPDATES='+json.dumps(str(updates)),
                        '-I'+str(HERE.parents[2]/'cc2-control/src'),new_source,'-o',new_binary],check=True,timeout=60)
                    new_app=new_binary.read_bytes();transaction=updates/sha(new_app);config=root/(arch+'-config.h')
                    old_files={n:('old '+n).encode() for n in NAMES};new_files={n:('new '+n).encode() for n in NAMES}
                    rows=['{%s,%s,%s,0%o}'%(json.dumps(n),json.dumps(sha(old_files[n])),json.dumps(sha(new_files[n])),0o700 if i<2 else 0o600) for i,n in enumerate(NAMES)]
                    config.write_text('static const struct au_plan au_expected={%s,%s,%s,{%s}};\n'%(
                        json.dumps(sha(old_app)),json.dumps(sha(new_app)),json.dumps(sha(profile)),','.join(rows)))
                    plan=plan_digest(sha(old_app),sha(new_app),sha(profile),old_files,new_files)
                    defines={'SC_BASE':str(component),'SC_PREFIX':str(tree),'SC_PROFILE_SHA':sha(profile),
                        'SC_APP_SHA':sha(old_app),'SC_NATIVE_SHA':sha(old_app),'SC_FRESH_SHA':sha(fresh),
                        'SC_FRESH_PATH':str(component/'fresh-guard.sh'),'SC_READY':str(folder/'ready'),
                        'SC_ORIGINAL':str(folder/'stock.bin'),'SC_RUN_SHA':sha((tree/'opt/bin/run_printer.sh').read_bytes()),
                        'SC_STRICT_SHA':sha((component/'libcc2-rfid.so').read_bytes()),
                        'AU_WORKER_CONFIG':str(config),'AU_COMPONENT':str(component),'AU_DIRECTORY':str(transaction),
                        'AU_APP':str(app),'AU_BARRIER':str(barrier),'CC2_MAINTENANCE_LOCK':str(barrier),
                        'CC2_CAVITY_CHANNEL':str(folder/'cavity-channel')}
                    for k,p in zip(['SC_REACTOR_SHA','SC_CAVITY_SHA','SC_NETWORK_SHA','SC_OLD_RFID_SHA'],MODULES):defines[k]=sha((tree/p[1:]).read_bytes())
                    actual=root/(arch+'-actual-worker')
                    subprocess.run([compiler,*FLAGS,*extra,'-ffunction-sections','-fdata-sections','-Wl,--gc-sections',
                        '-DSC_WITH_CAVITY=1','-DSC_VERIFY_CAVITY_CHANNEL=1','-DVC_PORT='+str(server.server_address[1]),
                        *['-D'+k+'='+json.dumps(v) for k,v in defines.items()],HERE/'app_binding_worker.c','-lm','-o',actual],check=True,timeout=120)
                    worker_bytes=actual.read_bytes()
                    if arch=='arm':
                        launcher_source=root/'launcher.c';launcher=root/'launcher'
                        launcher_source.write_text('#define _GNU_SOURCE\n#include <fcntl.h>\n#include <stdlib.h>\n#include <string.h>\n#include <unistd.h>\n#include "sha256.h"\nint main(int n,char**v){char h[65];int f=open('+json.dumps(str(actual))+',O_RDONLY|O_NOFOLLOW);if(f<0||!sha256_fd(f,h)||strcmp(h,"'+sha(worker_bytes)+'"))return 125;close(f);char**a=calloc((size_t)n+2,sizeof(char*));if(!a)return 125;a[0]="qemu-arm";a[1]='+json.dumps(str(actual))+';for(int i=1;i<n;i++)a[i+1]=v[i];execvp(a[0],a);return 125;}\n')
                        subprocess.run(['gcc',*FLAGS,'-I'+str(HERE.parent/'host'),launcher_source,'-o',launcher],check=True,timeout=60)
                        worker_bytes=launcher.read_bytes()
                    for case in cases:
                        server.mode='canvas-unavailable' if case=='canvas-unavailable' else 'host'
                        shutil.rmtree(folder);component,tree,native,app,olds,news,profile,fresh=fixture(folder,old_app,stock)
                        source=folder/'incoming';source.mkdir(mode=0o700);transaction.mkdir(parents=True,mode=0o700)
                        updates.chmod(0o700)
                        for i,n in enumerate(NAMES):put(component/n,old_files[n],0o700 if i<2 else 0o600);put(source/n,new_files[n],0o700 if i<2 else 0o600)
                        put(component/'original-stock-44.bin',stock)
                        modes=[0o755,0o700,0o600,0o700,0o700,0o700,0o644,0o600]
                        receipt=''
                        for i,path in enumerate(PATHS):
                            put(tree/path[1:],news[i],modes[i]);put(component/f'startup-tx/{i}.old',olds[i]);put(component/f'startup-tx/{i}.new',news[i])
                            receipt+=f'{i} {modes[i]:04o} {modes[i]:04o} {sha(olds[i])} {sha(news[i])} {tree/path[1:]}\n'
                        for name in ['ready','applied']:put(component/'startup-tx'/name,(sha(receipt.encode())+'\n').encode())
                        seed_journal(component,profile,5);original_journal=(component/'lifecycle').read_bytes()
                        put(transaction/'worker',worker_bytes,0o700)
                        descriptor={'protocol':1,'app_sha256':sha(new_app),'worker_sha256':sha(worker_bytes),'plan_sha256':plan}
                        put(transaction/'recovery.json',json.dumps(descriptor).encode())
                        barrier.mkdir(mode=0o700);held=os.open(barrier,os.O_RDONLY|os.O_DIRECTORY)
                        processes=[]
                        env=dict(os.environ,SC_CASE='first-guard-refused' if case=='guard-refused' else 'ready',SC_TRACE=str(folder/'fresh-count'))
                        def invoke(action,*args):
                            return subprocess.run([transaction/'worker',action,*map(str,args)],pass_fds=(held,),env=env,capture_output=True,text=True,timeout=15)
                        try:
                            a=spawn(app,[]);processes.append(a)
                            n=spawn(native,[tree/MODULES[i][1:] for i in range(3)]+[component/'libcc2-rfid.so']);processes.append(n)
                            put(folder/'ready',struct.pack('<8sQQQII',b'CC2RF1\0\0',123,0,0,n.pid,1))
                            put(folder/'cavity-channel/state',f'CC2CF1 {n.pid} 1122334455667788 1 0\n'.encode())
                            if case=='wrong-phase':seed_journal(component,profile,3)
                            if case=='bad-backup':put(component/'original-stock-44.bin',b'bad')
                            if case=='foreign-target':put(component/NAMES[3],b'foreign')
                            if case=='corrupt-descriptor':put(transaction/'recovery.json',b'{}')
                            before={key:(component/key).read_bytes() for key in NAMES}
                            if case=='retire-empty':
                                retired=invoke('--retire-inactive',held)
                                assert retired.returncode==0,(arch,case,retired.stdout,retired.stderr)
                                assert not transaction.exists() and list(updates.glob('.retired-*'))
                                assert {key:(component/key).read_bytes() for key in NAMES}==before
                                assert (component/'lifecycle').read_bytes()==original_journal and n.poll() is None
                                count+=1;continue
                            if case.startswith('installer-'):
                                from test_app_binding_installer_live import run as installer_live
                                os.close(held);held=-1;barrier.rmdir()
                                installer_live(folder,component,n,app,transaction,source,a,new_app,env,
                                    ['dash'] if '-dash-' in case else ['busybox','sh'],case.endswith('-failure'))
                                assert [(tree/p[1:]).read_bytes() for p in PATHS]==news,(arch,case,'startup changed')
                                assert n.poll() is None,(arch,case,'native stopped')
                                count+=1
                                continue
                            assert invoke('--check-current',held).returncode!=0,'old app must not qualify as current'
                            prepared=invoke('--prepare',source,held)
                            early=case in ['wrong-phase','bad-backup','guard-refused','foreign-target','corrupt-descriptor']
                            if early:
                                assert prepared.returncode!=0,(arch,case,prepared.stdout,prepared.stderr)
                                assert before=={key:(component/key).read_bytes() for key in NAMES}
                                assert app.read_bytes()==old_app and a.poll() is None
                            else:
                                assert prepared.returncode==0,(arch,case,prepared.stdout,prepared.stderr)
                                if case=='retire-pending':
                                    state=(transaction/'state').read_bytes()
                                    assert invoke('--retire-inactive',held).returncode!=0
                                    assert (transaction/'state').read_bytes()==state and not list(updates.glob('.retired-*'))
                                    assert {key:(component/key).read_bytes() for key in NAMES}==before
                                    count+=1;continue
                                assert invoke('--recover').returncode!=0,'recovery requires the new app parent'
                                a.terminate();a.wait(timeout=5);put(app,new_app,0o700)
                                if case in ['recover-without-barrier','foreign-barrier']:
                                    barrier.rmdir()
                                    if case=='foreign-barrier':barrier.mkdir(mode=0o700)
                                if case=='partial-recovery':
                                    for i in range(2):put(component/NAMES[i],new_files[NAMES[i]],0o700)
                                    payload=f'CC2RFUP1 2 2 {plan} {sha(original_journal)}\n'.encode()
                                    put(transaction/'state',payload+sha(payload).encode()+b'\n')
                                started=subprocess.Popen([app],stdout=subprocess.PIPE,stderr=subprocess.PIPE,text=True)
                                processes.append(started)
                                if case=='foreign-barrier':
                                    stdout,stderr=started.communicate(timeout=12)
                                    assert started.returncode!=0,(arch,case,stdout,stderr)
                                    assert {key:(component/key).read_bytes() for key in NAMES}==before
                                else:
                                    app_ready(started,(arch,case))
                                    assert {key:(component/key).read_bytes() for key in NAMES}==new_files
                                    assert invoke('--recover').returncode!=0,'a CLI must not impersonate the app bootstrap'
                                    if case=='rollback-live':
                                        assert invoke('--rollback',held).returncode!=0,'must stop app before rollback'
                                        assert {key:(component/key).read_bytes() for key in NAMES}==new_files
                                    if case=='recover-without-barrier':
                                        assert not barrier.exists();os.close(held);barrier.mkdir(mode=0o700);held=os.open(barrier,os.O_RDONLY|os.O_DIRECTORY)
                                    committed=invoke('--commit',held);assert committed.returncode==0,(arch,case,committed.stdout,committed.stderr)
                                    assert invoke('--check-current',held).returncode==0,'complete current app verification failed'
                                    started.terminate();started.wait(timeout=5)
                                    if case in ['rollback','rollback-live','rollback-retry','retire-rolled-back']:
                                        restored=invoke('--rollback',held);assert restored.returncode==0,(arch,case,restored.stderr)
                                        assert {key:(component/key).read_bytes() for key in NAMES}==old_files
                                        if case=='retire-rolled-back':
                                            fields=(transaction/'owner').read_text().splitlines()[0].split();fields[2]='99999999'
                                            payload=(' '.join(fields)+'\n').encode();put(transaction/'owner',payload+sha(payload).encode()+b'\n')
                                            put(app,old_app,0o700);again=spawn(app,[]);processes.append(again)
                                            retired=invoke('--retire-inactive',held)
                                            assert retired.returncode==0,(arch,case,retired.stdout,retired.stderr)
                                            assert not transaction.exists() and list(updates.glob('.retired-*'))
                                            assert {key:(component/key).read_bytes() for key in NAMES}==old_files
                                        if case=='rollback-retry':
                                            put(app,old_app,0o700);again=spawn(app,[]);processes.append(again)
                                            retried=invoke('--prepare',source,held)
                                            assert retried.returncode==0,(arch,case,retried.stderr)
                                            again.terminate();again.wait(timeout=5);put(app,new_app,0o700)
                                            resumed=start_app(app,(arch,case,'retry'));processes.append(resumed)
                                            assert {key:(component/key).read_bytes() for key in NAMES}==new_files
                                            assert invoke('--commit',held).returncode==0
                                    else:
                                        # A finalized update does not block legitimate later native removal.
                                        seed_journal(component,profile,10)
                                        if case in ['owner-dead','owner-nonempty']:
                                            fields=(transaction/'owner').read_text().splitlines()[0].split()
                                            fields[2]='99999999'
                                            payload=(' '.join(fields)+'\n').encode()
                                            put(transaction/'owner',payload+sha(payload).encode()+b'\n')
                                            if case=='owner-nonempty':put(barrier/'foreign',b'preserved evidence')
                                        restarted=start_app(app,(arch,case,'finalized restart'));processes.append(restarted)
                                        if case=='owner-dead':assert not barrier.exists(),'proven owned empty barrier not retired'
                                        if case=='owner-nonempty':assert (barrier/'foreign').read_bytes()==b'preserved evidence'
                            if case not in ['wrong-phase','upgrade','canvas-unavailable','owner-dead','owner-nonempty','recover-without-barrier','partial-recovery']:
                                assert (component/'lifecycle').read_bytes()==original_journal,(arch,case,'journal changed')
                            assert [(tree/p[1:]).read_bytes() for p in PATHS]==news,(arch,case,'startup changed')
                            assert n.poll() is None,(arch,case,'native stopped')
                        finally:
                            if held>=0:os.close(held)
                            for process in processes:
                                if process.poll() is None:process.terminate()
                                process.wait(timeout=5)
                        count+=1
            finally:server.shutdown();thread.join(timeout=5)
    print(f'PASS: {count} actual configured app-binding worker/startup host+ARM cases; qualified preflight, borrowed/own barrier, partial recovery, commit/rollback and retained native process/startup',flush=True)
    return count
if __name__=='__main__':
    assert sys.argv[1:] in [[],['--installer-only']]
    run(installer_only=bool(sys.argv[1:]))
