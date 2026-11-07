"""Actual scan preflight and original reader, with inert native processes.

Exercise the quiet ten-second MQTT heartbeat without changing readiness limits.
No scan is armed and no serial device or vendor executable is used.
"""
from pathlib import Path
import json
import os
import signal
import struct
import subprocess
import tempfile
import threading
import time

from test_post_boot import Server, Handler as CanvasHandler
from test_stage_worker import fixture, put, spawn, seed_journal, MODULES, PATHS
from hashlib import sha256

HERE=Path(__file__).resolve().parent
sha=lambda b:sha256(b).hexdigest()
CASES=['ready','quiet-refresh','quiet-timeout','heater-start','uds-lost',
       'too-old','cancelled','canvas-unavailable']


class Handler(CanvasHandler):
    def handle(self):
        request=self.request.recv(2048)
        path=request.split(b' ',2)[1]
        mode=self.server.mode
        if path==b'/api/canvas':
            # Same Canvas schema as the normal post-boot integration fixture.
            from test_post_boot import BASE
            body=dict(BASE)
            if mode=='canvas-unavailable':body['available']=False
            self.server.hints+=1
        elif path==b'/api/printer':
            self.server.reads+=1
            age=0 if mode in ['ready','canvas-unavailable'] else 16 if mode=='too-old' else 10
            if mode=='quiet-refresh' and self.server.reads>=3:age=0
            target=200 if mode=='heater-start' and self.server.reads>=2 else 0
            body={'connected':True,'last_message_age':age,'machine':{'status':1},
                  'extruder':{'temperature':24,'target':target},
                  'heater_bed':{'temperature':20,'target':0},
                  'print':{'enabled':False,'native':{'state':'standby'}},
                  'recovery':{'available':False,'reboot_pending':False},
                  'bed_drying':{'active':False}}
        elif path==b'/api/uds':
            body={'connected':mode!='uds-lost','fresh':True,
                  'values':{'nozzle_temperature':24,'bed_temperature':20,
                            'nozzle_target':0,'bed_target':0,'live_velocity':0},
                  'print_state':'standby'}
        else:return
        data=json.dumps(body,separators=(',',':')).encode()
        response=b'HTTP/1.0 200 OK\r\nContent-Length: '+str(len(data)).encode()+b'\r\nConnection: close\r\n\r\n'+data
        try:self.request.sendall(response)
        except (BrokenPipeError,ConnectionResetError):pass


def run(stock_bootstrap=False,with_cavity=False):
    stock=Path('/stock-canvas.bin').read_bytes();count=0
    flags=['-O2','-std=c11','-Wall','-Wextra','-Wpedantic','-Werror','-D_POSIX_C_SOURCE=200809L']
    with tempfile.TemporaryDirectory(prefix='cc2-scan-readiness-') as temp:
        root=Path(temp);source=root/'dummy.c';dummy=root/'dummy'
        source.write_text('#include <fcntl.h>\n#include <stdio.h>\n#include <sys/mman.h>\n#include <unistd.h>\nint main(int n,char**v){for(int i=1;i<n;i++){int f=open(v[i],O_RDONLY);if(f<0||mmap(0,4096,PROT_READ,MAP_PRIVATE,f,0)==MAP_FAILED)return 2;close(f);}puts("READY");fflush(stdout);for(;;)pause();}\n')
        subprocess.run(['gcc',*flags,source,'-o',dummy],check=True,timeout=60)
        with Server(('127.0.0.1',0),Handler) as server:
            server.mode='ready';server.reads=server.hints=0
            thread=threading.Thread(target=server.serve_forever,daemon=True);thread.start()
            try:
                for arch,cc,extra,runner in [
                    ('host','gcc',[],[]),
                    ('arm','arm-linux-gnueabihf-gcc',['-static','-D_TIME_BITS=32','-D_FILE_OFFSET_BITS=32'],['qemu-arm']),
                    ('sanitizers','gcc',['-fsanitize=address,undefined','-fno-sanitize-recover=all','-fno-pie','-no-pie'],[])]:
                    folder=root/arch
                    component,tree,native,app,old,new,profile,_=fixture(folder,dummy.read_bytes(),stock)
                    reader=root/(arch+'-reader')
                    subprocess.run([cc,*flags,*extra,'-DVC_PORT='+str(server.server_address[1]),
                        HERE/'fresh_reader.c','-lm','-o',reader],check=True,timeout=120)
                    # Exec the original reader for the chosen architecture.
                    # The script only supplies qemu for the inert ARM fixture.
                    fresh=('#!/bin/sh\nexec '+ ' '.join([*runner,str(reader)])+' "$@"\n').encode()
                    put(component/'fresh-guard.sh',fresh,0o700)
                    defines={'SC_BASE':str(component),'SC_PREFIX':str(tree),'SC_PROFILE_SHA':sha(profile),
                        'SC_APP_SHA':sha(dummy.read_bytes()),'SC_NATIVE_SHA':sha(dummy.read_bytes()),
                        'SC_FRESH_SHA':sha(fresh),'SC_RUN_SHA':sha((tree/'opt/bin/run_printer.sh').read_bytes()),
                        'SC_READY':str(folder/'ready'),'SC_ORIGINAL':str(folder/'stock.bin'),
                        'CC2_MAINTENANCE_LOCK':str(folder/'install.lock'),
                        'CC2_CAVITY_CHANNEL':str(folder/'cavity-channel'),
                        'SC_STRICT_SHA':sha((component/'libcc2-rfid.so').read_bytes())}
                    for key,path in zip(['SC_REACTOR_SHA','SC_CAVITY_SHA','SC_NETWORK_SHA','SC_OLD_RFID_SHA'],MODULES):
                        defines[key]=sha((tree/path[1:]).read_bytes())
                    binary=root/(arch+'-coordinator')
                    subprocess.run([cc,*flags,*extra,'-DVC_PORT='+str(server.server_address[1]),
                        *['-D'+k+'='+json.dumps(v) for k,v in defines.items()],
                        *(['-DSC_STOCK_BOOTSTRAP=1',f'-DSC_WITH_CAVITY={int(with_cavity)}','-DSC_CAVITY_BOOTSTRAP_OPTIONAL=1','-DSC_VERIFY_CAVITY_CHANNEL=1'] if stock_bootstrap else []),
                        '-I'+str(HERE.parent/'host'),HERE/'coordinator.c','-lm','-o',binary],check=True,timeout=120)
                    processes=[]
                    try:
                        a=spawn(app,[]);processes.append(a)
                        n=spawn(native,[tree/MODULES[i][1:] for i in ([0,2] if stock_bootstrap else range(4))]);processes.append(n)
                        put(folder/'ready',struct.pack('<8sQQQII',b'CC2RF1\0\0',123,0,0,n.pid,1))
                        server.mode='ready';server.reads=server.hints=0
                        subprocess.run([*runner,binary,'--stage-host'],check=True,capture_output=True,timeout=15)
                        n.terminate();n.wait(timeout=5)
                        n=spawn(native,[*[tree/p[1:] for p in MODULES[:3]],component/'libcc2-rfid.so']);processes.append(n)
                        put(folder/'ready',struct.pack('<8sQQQII',b'CC2RF1\0\0',456,0,0,n.pid,1))
                        if with_cavity:put(folder/'cavity-channel/state',f'CC2CF1 {n.pid} 1122334455667788 1 0\n'.encode())
                        seed_journal(component,profile,5)
                        before=(component/'lifecycle').read_bytes()
                        targets=[(tree/p[1:]).read_bytes() for p in PATHS]
                        for case in CASES:
                            server.mode=case;server.reads=server.hints=0
                            start=time.monotonic()
                            child=subprocess.Popen([*runner,binary,'--check-active'],stdout=subprocess.PIPE,stderr=subprocess.PIPE,text=True)
                            if case=='cancelled':
                                deadline=time.monotonic()+5
                                while server.hints==0 and child.poll() is None and time.monotonic()<deadline:time.sleep(.01)
                                assert server.hints>0,(arch,'never reached wait')
                                child.send_signal(signal.SIGTERM)
                            stdout,stderr=child.communicate(timeout=18)
                            elapsed=time.monotonic()-start
                            assert (child.returncode==0)==(case in ['ready','quiet-refresh']),(arch,case,stdout,stderr)
                            if case=='quiet-refresh':assert server.hints>=1 and server.reads>=5
                            if case=='quiet-timeout':assert 9<=elapsed<=16
                            if case in ['too-old','heater-start','uds-lost','cancelled']:assert elapsed<5,(arch,case,elapsed)
                            assert (component/'lifecycle').read_bytes()==before,(arch,case,'journal changed')
                            assert [(tree/p[1:]).read_bytes() for p in PATHS]==targets
                            assert a.poll() is None and n.poll() is None
                            assert not (folder/'install.lock').exists(),(arch,case,'barrier leaked')
                            count+=1
                    finally:
                        for p in processes:
                            if p.poll() is None:p.terminate()
                            p.wait(timeout=5)
            finally:server.shutdown();thread.join(timeout=5)
    print(f'PASS: {count} actual scan-preflight/original-reader cases host/ARM/sanitizers; quiet heartbeat, unsafe transitions, timeout/cancellation, native process and journal preserved')


if __name__=='__main__':run()
