"""Combined stage/post-boot coordinator; dummy processes and loopback Canvas.

Journal ACK phases are explicit fixtures, not claims of a physical transfer.
Actual transfer/recovery core qualification is separate. No UART is opened.
"""
from pathlib import Path
import copy
import hashlib
import json
import os
import shutil
import socketserver
import struct
import subprocess
import tempfile
import threading

from test_stage_worker import fixture,put,spawn,seed_journal,PATHS,MODULES
HERE=Path(__file__).resolve().parent
sha=lambda b:hashlib.sha256(b).hexdigest()
CASES=['host','filter','stock','idempotent','same-boot','wrong-phase','canvas-unavailable',
       'canvas-disconnected','canvas-error','wrong-source','wrong-canvas-id','duplicate-canvas-id',
       'duplicate-json','http-error','length-mismatch','duplicate-length','chunked','truncated','oversized',
       'strict-missing','old-guard','target-changed','target-mixed','backup-bad','applied-missing',
       'guard-refused','app-missing','busy','route-changed']
BASE={'available':True,'machine_status':1,'source':'mqtt','telemetry':{'result':{
      'error_code':0,'canvas_info':{'active_canvas_id':0,'canvas_list':[{'canvas_id':0,'connected':1}]}}}}

class Server(socketserver.ThreadingTCPServer):
    allow_reuse_address=True
    daemon_threads=True

class Handler(socketserver.BaseRequestHandler):
    def handle(self):
        self.request.settimeout(5);request=b''
        while b'\r\n\r\n' not in request:
            part=self.request.recv(512)
            if not part:return
            request+=part
            if len(request)>2048:return
        if not request.startswith(b'GET /api/canvas HTTP/1.0\r\n'):return
        case=self.server.mode
        data=copy.deepcopy(BASE);info=data['telemetry']['result']['canvas_info']
        if case=='canvas-unavailable':data['available']=False
        if case=='canvas-disconnected':info['canvas_list'][0]['connected']=0
        if case=='canvas-error':data['telemetry']['result']['error_code']=1
        if case=='wrong-source':data['source']='cached'
        if case=='wrong-canvas-id':info['active_canvas_id']=1
        if case=='duplicate-canvas-id':info['canvas_list'].append(copy.deepcopy(info['canvas_list'][0]))
        body=json.dumps(data,separators=(',',':')).encode()
        if case=='duplicate-json':body=body.replace(b'"available":true',b'"available":true,"available":true')
        if case=='oversized':body=b' '*40000+body
        status=b'500 Error' if case=='http-error' else b'200 OK'
        length=len(body)+(1 if case=='length-mismatch' else 0)
        header=b'HTTP/1.0 '+status+b'\r\nContent-Length: '+str(length).encode()+b'\r\n'
        if case=='duplicate-length':header+=b'Content-Length: '+str(length).encode()+b'\r\n'
        if case=='chunked':header+=b'Transfer-Encoding: chunked\r\n'
        response=header+b'Connection: close\r\n\r\n'+(body[:-9] if case=='truncated' else body)
        try:
            # Deliberately fragment status and header; no single read promise.
            for part in [response[:7],response[7:29],response[29:]]:self.request.sendall(part)
        except (BrokenPipeError,ConnectionResetError):pass

def run(stock_bootstrap=False):
    stock=Path('/stock-canvas.bin').read_bytes();count=0
    flags=['-O2','-std=c11','-Wall','-Wextra','-Wpedantic','-Werror','-D_POSIX_C_SOURCE=200809L']
    with tempfile.TemporaryDirectory(prefix='cc2-post-boot-') as temporary:
        root=Path(temporary);dummy_source=root/'dummy.c';dummy=root/'dummy'
        dummy_source.write_text('#include <fcntl.h>\n#include <stdio.h>\n#include <sys/mman.h>\n#include <unistd.h>\nint main(int n,char**v){for(int i=1;i<n;i++){int f=open(v[i],O_RDONLY);if(f<0||mmap(0,4096,PROT_READ,MAP_PRIVATE,f,0)==MAP_FAILED)return 2;close(f);}puts("READY");fflush(stdout);for(;;)pause();}\n')
        subprocess.run(['gcc',*flags,dummy_source,'-o',dummy],check=True,timeout=60);data=dummy.read_bytes()
        with Server(('127.0.0.1',0),Handler) as server:
            server.mode='host';thread=threading.Thread(target=server.serve_forever,daemon=True);thread.start()
            try:
                for arch,compiler,extra,runner in [
                    ('host','gcc',[],[]),('arm','arm-linux-gnueabihf-gcc',['-static','-D_TIME_BITS=32','-D_FILE_OFFSET_BITS=32'],['qemu-arm']),
                    ('sanitizers','gcc',['-fsanitize=address,undefined','-fno-sanitize-recover=all','-fno-pie','-no-pie'],[]),
                ]:
                    folder=root/arch
                    component,tree,native,app,old,new,profile,fresh=fixture(folder,data,stock)
                    defines={'SC_BASE':str(component),'SC_PREFIX':str(tree),'SC_PROFILE_SHA':sha(profile),
                        'SC_APP_SHA':sha(data),'SC_NATIVE_SHA':sha(data),'SC_FRESH_SHA':sha(fresh),
                        'SC_RUN_SHA':sha((tree/'opt/bin/run_printer.sh').read_bytes()),
                        'SC_READY':str(folder/'ready'),'SC_ORIGINAL':str(folder/'stock.bin'),
                        'CC2_MAINTENANCE_LOCK':str(folder/'install.lock'),
                        'SC_STRICT_SHA':sha((component/'libcc2-rfid.so').read_bytes())}
                    for key,path in zip(['SC_REACTOR_SHA','SC_CAVITY_SHA','SC_NETWORK_SHA','SC_OLD_RFID_SHA'],MODULES):defines[key]=sha((tree/path[1:]).read_bytes())
                    binary=root/(arch+'-coordinator')
                    subprocess.run([compiler,*flags,*extra,'-DVC_PORT='+str(server.server_address[1]),
                        *['-D'+key+'='+json.dumps(value) for key,value in defines.items()],
                        *(['-DSC_STOCK_BOOTSTRAP=1','-DSC_WITH_CAVITY=0'] if stock_bootstrap else []),
                        '-I'+str(HERE.parent/'host'),HERE/'coordinator.c','-lm','-o',binary],check=True,timeout=120)
                    for case in CASES:
                        shutil.rmtree(folder);component,tree,native,app,old,new,profile,fresh=fixture(folder,data,stock)
                        processes=[]
                        try:
                            a=spawn(app,[]);processes.append(a)
                            n=spawn(native,[tree/MODULES[i][1:] for i in ([0,2] if stock_bootstrap else range(4))]);processes.append(n)
                            put(folder/'ready',struct.pack('<8sQQQII',b'CC2RF1\0\0',123,0,0,n.pid,1))
                            env=dict(os.environ,SC_CASE='ready',SC_TRACE=str(folder/'fresh-count'))
                            staged=subprocess.run([*runner,binary,'--stage-host'],env=env,capture_output=True,text=True,timeout=10)
                            assert staged.returncode==0,(arch,case,'stage',staged.stdout,staged.stderr)
                            n.terminate();n.wait(timeout=5)
                            modules=[tree/MODULES[i][1:] for i in ([0,2] if stock_bootstrap else range(3))]
                            if case=='old-guard':modules.append(tree/MODULES[3][1:])
                            elif case!='strict-missing':modules.append(component/'libcc2-rfid.so')
                            n=spawn(native,modules);processes.append(n)
                            put(folder/'ready',struct.pack('<8sQQQII',b'CC2RF1\0\0',456,0,0,n.pid,1))
                            phase=4 if case=='filter' else 7 if case=='stock' else 2 if case=='idempotent' else 5 if case=='wrong-phase' else 1
                            seed_journal(component,profile,phase)
                            if case=='same-boot':
                                journal=bytearray((component/'lifecycle').read_bytes()[:128]);journal[48:84]=Path('/proc/sys/kernel/random/boot_id').read_text().strip().encode()
                                put(component/'lifecycle',journal+sha(journal).encode())
                            if case=='target-changed':put(tree/PATHS[7][1:],b'foreign')
                            if case=='target-mixed':put(tree/PATHS[7][1:],old[7])
                            if case=='backup-bad':put(component/'original-stock-44.bin',b'bad')
                            if case=='applied-missing':(component/'startup-tx/applied').unlink()
                            if case=='guard-refused':env['SC_CASE']='first-guard-refused'
                            if case=='app-missing':a.terminate();a.wait(timeout=5)
                            if case=='busy':(folder/'install.lock').mkdir(mode=0o700)
                            if case=='route-changed':put(tree/'opt/bin/run_printer.sh',b'changed',0o755)
                            server.mode=case
                            before=(component/'lifecycle').read_bytes();targets=[(tree/p[1:]).read_bytes() for p in PATHS]
                            action='--verify-filter-boot' if case=='filter' else '--verify-stock-boot' if case=='stock' else '--verify-host-boot'
                            result=subprocess.run([*runner,binary,action],env=env,capture_output=True,text=True,timeout=12)
                            okay=case in ['host','filter','stock','idempotent']
                            assert (result.returncode==0)==okay,(arch,case,result.returncode,result.stdout,result.stderr)
                            after=(component/'lifecycle').read_bytes()
                            if okay:
                                assert after[16]==(5 if case=='filter' else 8 if case=='stock' else 2)
                                repeated=subprocess.run([*runner,binary,action],env=env,capture_output=True,text=True,timeout=12)
                                assert repeated.returncode==0 and (component/'lifecycle').read_bytes()==after,(arch,case,'not idempotent',repeated.stderr)
                            else:assert after==before,(arch,case,'journal advanced')
                            assert [(tree/p[1:]).read_bytes() for p in PATHS]==targets,(arch,case,'verification changed targets')
                            if case!='busy':assert not (folder/'install.lock').exists()
                            assert n.poll() is None and (a.poll() is None or case=='app-missing')
                            count+=1
                        finally:
                            for process in processes:
                                if process.poll() is None:process.terminate()
                                process.wait(timeout=5)
            finally:server.shutdown();thread.join(timeout=5)
    print(f'PASS: {count} actual post-boot coordinator cases on host/ARM/sanitizers; kernel boot UUID, strict native guard, exact targets/backup and live Canvas required')
    return count
if __name__=='__main__':run()
