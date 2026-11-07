"""Actual coordinator -> actual normal worker -> PTY/RAM Canvas.

Kernel boot IDs and native/app processes are fixtures. No vendor executable,
physical UART or printer is accessed. ARM child exec uses an explicit emulator
launcher because Docker need not provide kernel binfmt registration.
"""
from pathlib import Path
import errno
import hashlib
import json
import os
import pty
import select
import signal
import struct
import subprocess
import tempfile
import threading
import time

from test_stage_worker import fixture,put,spawn,MODULES,PATHS
from test_post_boot import Server,Handler
HERE=Path(__file__).resolve().parent
sha=lambda b:hashlib.sha256(b).hexdigest()
CASES=['install','cycle','bad-offset','cancel','guard-refused','wrong-phase','changed-worker','busy']

def crc(data):
    value=0xffff
    for b in data:
        value^=b
        for _ in range(8):value=(value>>1)^(0x8408 if value&1 else 0)
    return value
def packet(command,data):return b'\xa5\x5a'+bytes([command])+len(data).to_bytes(2,'big')+data+crc(data).to_bytes(2,'big')

class Canvas:
    def __init__(self,master,stock,case):
        self.master=master;self.stock=stock;self.case=case;self.process=None
        self.memory=bytearray(b'B'*0x4000+stock+b'\xff'*(53248-len(stock)))
        self.erases=0;self.runs=[];self.errors=[];self.frames=[];self.stop=threading.Event()
        self.thread=threading.Thread(target=self.peer,daemon=True);self.thread.start()
    def peer(self):
        data=b'';entered=False;offset=0;injected=False
        try:
            while not self.stop.is_set():
                if not select.select([self.master],[],[],.02)[0]:continue
                try:part=os.read(self.master,4096)
                except OSError as error:
                    if error.errno==errno.EIO:time.sleep(.01);continue
                    raise
                if not part:continue
                data+=part
                if not entered:
                    magic=b' \x1c Request Serial Bootloader!! ~'
                    if len(data)<len(magic):continue
                    assert data.startswith(magic),'bad boot request'
                    data=data[len(magic):];entered=True
                while len(data)>=5:
                    # A new normal transfer after a simulated reboot starts
                    # with the non-writing application boot request again.
                    magic=b' \x1c Request Serial Bootloader!! ~'
                    if data.startswith(magic):data=data[len(magic):];offset=0;continue
                    if data.startswith(magic[:min(len(data),len(magic))]) and len(data)<len(magic):break
                    assert data[:2]==b'\xa5\x5a'
                    length=int.from_bytes(data[3:5],'big');assert length<=1036
                    if len(data)<length+7:break
                    frame,data=data[:length+7],data[length+7:];command=frame[2];payload=frame[5:5+length]
                    assert crc(payload)==int.from_bytes(frame[-2:],'big')
                    self.frames.append(command)
                    if command==3:os.write(self.master,packet(3,payload));continue
                    if command==2:
                        assert offset==len(self.stock),'jump before full image'
                        self.runs.append(bytes(self.memory[0x4000:0x4000+len(self.stock)]));continue
                    if command==0:
                        assert payload==struct.pack('<I',len(self.stock))
                        self.erases+=1;offset=0;self.memory[0x4000:]=b'\xff'*53248
                        os.write(self.master,packet(0,struct.pack('<I',53248)));continue
                    assert command==1 and length>=16
                    position,size,total=struct.unpack_from('<III',payload)
                    assert position==offset and 4<=size<=1024 and size%4==0 and total==len(self.stock) and length==size+12 and position+size<=len(self.stock)
                    self.memory[0x4000+position:0x4000+position+size]=payload[12:];offset+=size
                    if offset==10240 and not injected and self.case in ['bad-offset','cancel']:
                        injected=True
                        if self.case=='cancel':
                            assert self.process is not None
                            os.kill(self.process.pid,signal.SIGTERM)
                            continue
                        os.write(self.master,packet(1,struct.pack('<II',size,offset-4)));continue
                    os.write(self.master,packet(1,struct.pack('<II',size,offset)))
        except Exception as error:self.errors.append(repr(error))
    def close(self):self.stop.set();self.thread.join(timeout=3)

def run():
    stock=Path('/stock-canvas.bin').read_bytes();patched=stock[:0x88d4]+bytes.fromhex('22e000bf')+stock[0x88d8:]
    flags=['-O2','-std=c11','-Wall','-Wextra','-Wpedantic','-Werror','-D_POSIX_C_SOURCE=200809L']
    count=0
    with tempfile.TemporaryDirectory(prefix='cc2-coordinated-flow-') as temporary:
        root=Path(temporary);dummy=root/'dummy';dummy_source=root/'dummy.c'
        dummy_source.write_text('#include <fcntl.h>\n#include <stdio.h>\n#include <sys/mman.h>\n#include <unistd.h>\nint main(int n,char**v){for(int i=1;i<n;i++){int f=open(v[i],O_RDONLY);if(f<0||mmap(0,4096,PROT_READ,MAP_PRIVATE,f,0)==MAP_FAILED)return 2;close(f);}puts("READY");fflush(stdout);for(;;)pause();}\n')
        subprocess.run(['gcc',*flags,dummy_source,'-o',dummy],check=True,timeout=60);dummy_bytes=dummy.read_bytes()
        with Server(('127.0.0.1',0),Handler) as server:
            server.mode='host';http_thread=threading.Thread(target=server.serve_forever,daemon=True);http_thread.start()
            try:
                for arch,compiler,extra,runner in [('host','gcc',[],[]),('arm','arm-linux-gnueabihf-gcc',['-static','-D_TIME_BITS=32','-D_FILE_OFFSET_BITS=32'],['qemu-arm'])]:
                    for case in CASES:
                        folder=root/f'{arch}-{case}'
                        component,tree,native,app,old,new,profile,fresh=fixture(folder,dummy_bytes,stock);folder.chmod(0o700)
                        boot_file=folder/'kernel-boot-id';put(boot_file,Path('/proc/sys/kernel/random/boot_id').read_bytes())
                        master,slave=pty.openpty();device=os.ttyname(slave);os.close(slave)
                        memory=Canvas(master,stock,case);processes=[]
                        try:
                            worker=component/'worker.arm' if runner else component/'transfer-worker'
                            defines={'CV_BASE':str(component),'CV_ORIGINAL':str(folder/'stock.bin'),'CV_CHANNEL':str(folder),
                                'CV_MODULE':str(component/'libcc2-rfid.so'),'CV_MODULE_SHA':sha((component/'libcc2-rfid.so').read_bytes()),
                                'CV_EXE_SHA':sha(dummy_bytes),'CV_GUARD':str(component/'fresh-guard.sh'),'CV_GUARD_SHA':sha(fresh),
                                'CV_TTY':device,'CV_BOOT_ID':str(boot_file)}
                            subprocess.run([compiler,*flags,*extra,'-DCV_COORDINATED=1',
                                *['-D'+key+'='+json.dumps(value) for key,value in defines.items()],
                                '-I'+str(HERE.parent/'host'),HERE.parent/'transfer/canvas_worker.c','-o',worker],check=True,timeout=120)
                            worker.chmod(0o700)
                            if runner:
                                put(component/'transfer-worker',f'#!/bin/sh\nset -eu\n[ "$(sha256sum "{worker}" | cut -d " " -f 1)" = {sha(worker.read_bytes())} ] || exit 125\nexec qemu-arm "{worker}" "$@"\n'.encode(),0o700)
                            config={'SC_BASE':str(component),'SC_PREFIX':str(tree),'SC_PROFILE_SHA':sha(profile),
                                'SC_APP_SHA':sha(dummy_bytes),'SC_NATIVE_SHA':sha(dummy_bytes),'SC_FRESH_SHA':sha(fresh),
                                'SC_RUN_SHA':sha((tree/'opt/bin/run_printer.sh').read_bytes()),
                                'SC_READY':str(folder/'ready'),'SC_ORIGINAL':str(folder/'stock.bin'),'SC_BOOT_ID':str(boot_file),
                                'CC2_MAINTENANCE_LOCK':str(folder/'install.lock'),
                                'SC_STRICT_SHA':sha((component/'libcc2-rfid.so').read_bytes()),
                                'SC_TRANSFER_SHA':sha((component/'transfer-worker').read_bytes())}
                            for key,path in zip(['SC_REACTOR_SHA','SC_CAVITY_SHA','SC_NETWORK_SHA','SC_OLD_RFID_SHA'],MODULES):config[key]=sha((tree/path[1:]).read_bytes())
                            coordinator=folder/'coordinator'
                            subprocess.run([compiler,*flags,*extra,'-DVC_PORT='+str(server.server_address[1]),
                                *['-D'+key+'='+json.dumps(value) for key,value in config.items()],
                                '-I'+str(HERE.parent/'host'),HERE/'coordinator.c','-lm','-o',coordinator],check=True,timeout=120)
                            a=spawn(app,[]);processes.append(a)
                            n=spawn(native,[tree/p[1:] for p in MODULES]);processes.append(n)
                            put(folder/'ready',struct.pack('<8sQQQII',b'CC2RF1\0\0',123,0,0,n.pid,1))
                            env=dict(os.environ,SC_CASE='ready',SC_TRACE=str(folder/'fresh-count'))
                            def invoke(action,timeout=30):
                                return subprocess.run([*runner,coordinator,action],env=env,capture_output=True,text=True,timeout=timeout)
                            staged=invoke('--stage-host');assert staged.returncode==0,(arch,case,staged.stdout,staged.stderr)
                            n.terminate();n.wait(timeout=5)
                            n=spawn(native,[tree/p[1:] for p in MODULES[:3]]+[component/'libcc2-rfid.so']);processes.append(n)
                            put(folder/'ready',struct.pack('<8sQQQII',b'CC2RF1\0\0',456,0,0,n.pid,1))
                            put(boot_file,b'00000000-0000-0000-0000-000000000002\n')
                            if case!='install':
                                verified=invoke('--verify-host-boot');assert verified.returncode==0,(arch,case,verified.stdout,verified.stderr)
                            before=(component/'lifecycle').read_bytes()
                            if case=='guard-refused':env['SC_CASE']='first-guard-refused'
                            if case=='wrong-phase':
                                from test_stage_worker import seed_journal
                                seed_journal(component,profile,1);before=(component/'lifecycle').read_bytes()
                            if case=='changed-worker':put(component/'transfer-worker',(component/'transfer-worker').read_bytes()+b'changed',0o700)
                            if case=='busy':(folder/'install.lock').mkdir(mode=0o700)
                            action='--advance' if case=='install' else '--install-filter'
                            active=subprocess.Popen([*runner,coordinator,action],env=env,stdout=subprocess.PIPE,stderr=subprocess.PIPE,text=True)
                            memory.process=active;stdout,stderr=active.communicate(timeout=30)
                            success=case in ['install','cycle']
                            assert (active.returncode==0)==success,(arch,case,stdout,stderr,memory.errors)
                            if case in ['install','cycle','bad-offset','cancel']:
                                deadline=time.monotonic()+1
                                while len(memory.runs)<1 and not memory.errors and time.monotonic()<deadline:time.sleep(.01)
                                assert n.poll() is not None and a.poll() is None
                                assert (component/'lifecycle').read_bytes()[16]==(4 if success else 7)
                                assert memory.runs==([patched] if success else [stock]),(arch,case,stdout,stderr,memory.runs,memory.errors)
                                assert (folder/'install.lock').is_dir()
                                state=json.loads(invoke('--status').stdout);assert not state['busy'] and state['reboot_required'] and state['barrier']
                                old_erases=memory.erases
                                waiting=invoke('--advance');assert waiting.returncode==0 and memory.erases==old_erases
                                # Simulated complete boot clears only the owned,
                                # empty volatile lock. Persistent evidence stays.
                                os.rmdir(folder/'install.lock')
                                n=spawn(native,[tree/p[1:] for p in MODULES[:3]]+[component/'libcc2-rfid.so']);processes.append(n)
                                put(folder/'ready',struct.pack('<8sQQQII',b'CC2RF1\0\0',789,0,0,n.pid,1))
                                put(boot_file,b'00000000-0000-0000-0000-000000000003\n')
                                checked=invoke('--verify-filter-boot' if success else '--verify-stock-boot')
                                assert checked.returncode==0,(arch,case,checked.stdout,checked.stderr)
                                assert (component/'lifecycle').read_bytes()[16]==(5 if success else 8)
                                stable=invoke('--advance');assert stable.returncode==0 and memory.erases==old_erases
                                if case=='cycle':
                                    untouched_frames=len(memory.frames)
                                    checked=invoke('--check-active')
                                    assert checked.returncode==0,(arch,checked.stdout,checked.stderr)
                                    assert len(memory.frames)==untouched_frames and (component/'lifecycle').read_bytes()[16]==5
                                    restored=invoke('--restore-original');assert restored.returncode==0,(arch,restored.stdout,restored.stderr,memory.errors)
                                    deadline=time.monotonic()+1
                                    while len(memory.runs)<2 and not memory.errors and time.monotonic()<deadline:time.sleep(.01)
                                    assert memory.runs==[patched,stock] and (component/'lifecycle').read_bytes()[16]==7
                                    os.rmdir(folder/'install.lock');put(boot_file,b'00000000-0000-0000-0000-000000000004\n')
                                    n=spawn(native,[tree/p[1:] for p in MODULES[:3]]+[component/'libcc2-rfid.so']);processes.append(n)
                                    put(folder/'ready',struct.pack('<8sQQQII',b'CC2RF1\0\0',890,0,0,n.pid,1))
                                    assert invoke('--verify-stock-boot').returncode==0
                                    assert (component/'lifecycle').read_bytes()[16]==8
                                    # Explicit host removal follows a verified
                                    # stock boot; it is never an automatic side
                                    # effect of observing phase 8.
                                    frames_before=len(memory.frames)
                                    assert invoke('--advance').returncode==0
                                    assert (component/'lifecycle').read_bytes()[16]==8
                                    removed=invoke('--remove-host')
                                    assert removed.returncode==0,(arch,removed.stdout,removed.stderr)
                                    assert (component/'lifecycle').read_bytes()[16]==9
                                    for i,path in enumerate(PATHS):assert (tree/path[1:]).read_bytes()==old[i]
                                    assert invoke('--advance').returncode==0
                                    assert (component/'lifecycle').read_bytes()[16]==9
                                    n.terminate();n.wait(timeout=5)
                                    n=spawn(native,[tree/p[1:] for p in MODULES]);processes.append(n)
                                    put(folder/'ready',struct.pack('<8sQQQII',b'CC2RF1\0\0',901,0,0,n.pid,1))
                                    put(boot_file,b'00000000-0000-0000-0000-000000000005\n')
                                    disabled=invoke('--advance')
                                    assert disabled.returncode==0,(arch,disabled.stdout,disabled.stderr)
                                    assert (component/'lifecycle').read_bytes()[16]==10
                                    assert invoke('--advance').returncode==0
                                    assert len(memory.frames)==frames_before
                                    # Reactivation reuses exact verified backup
                                    # and transaction, but requires a new boot.
                                    previous=(component/'lifecycle').read_bytes()
                                    staged=invoke('--stage-host')
                                    assert staged.returncode==0,(arch,staged.stdout,staged.stderr)
                                    after=(component/'lifecycle').read_bytes()
                                    assert after[16]==1 and after[32:48]==previous[32:48]
                                    assert int.from_bytes(after[24:32],'little')>int.from_bytes(previous[24:32],'little')
                                    assert after[48:84]==b'00000000-0000-0000-0000-000000000005'
                                    for i,path in enumerate(PATHS):assert (tree/path[1:]).read_bytes()==new[i]
                                    assert invoke('--advance').returncode==0
                                    assert (component/'lifecycle').read_bytes()[16]==1 and len(memory.frames)==frames_before
                            else:
                                assert not memory.frames and n.poll() is None and a.poll() is None
                                assert (component/'lifecycle').read_bytes()==before
                                if case!='busy':assert not (folder/'install.lock').exists()
                            assert not memory.errors,(arch,case,memory.errors)
                            assert bytes(memory.memory[:0x4000])==b'B'*0x4000
                            assert (component/'original-stock-44.bin').read_bytes()==stock
                            count+=1
                        finally:
                            memory.close();os.close(master)
                            for process in processes:
                                if process.poll() is None:process.terminate()
                                process.wait(timeout=5)
            finally:server.shutdown();http_thread.join(timeout=5)
    print(f'PASS: {count} actual coordinator/transfer-worker host+ARM flows, PTY/RAM only; install, reboot verification, full restore, offset recovery, cancellation and preflight refusal')
    return count
if __name__=='__main__':run()
