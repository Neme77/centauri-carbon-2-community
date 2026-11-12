"""Actual device worker, dummy native process and PTY/memory Canvas only.

The dummy native executable is a host pause loop, also when the worker is ARM.
No vendor executable, real UART, MCU or printer service is used.
"""
from pathlib import Path
import errno
import fcntl
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

HERE=Path(__file__).resolve().parent
src=HERE.parent/'transfer'
from test_recovery import seed
stock=Path('/stock-canvas.bin').read_bytes()
assert len(stock)==51472 and hashlib.sha256(stock).hexdigest()=='f1267ee3219dcad73efffa8dc38e30ff0f17c02c0a1674cbead48a99bdff3228'
patched=stock[:0x88d4]+bytes.fromhex('22e000bf')+stock[0x88d8:]
sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
common=['-O2','-UNDEBUG','-std=c11','-Wall','-Wextra','-Wpedantic','-Werror','-D_POSIX_C_SOURCE=200809L','-I'+str(HERE.parent/'host')]
architectures=[('host','gcc',[],[]),('arm','arm-linux-gnueabihf-gcc',
    ['-static','-marm','-mcpu=cortex-a7','-mfpu=neon-vfpv4','-mfloat-abi=hard','-D_TIME_BITS=32','-D_FILE_OFFSET_BITS=32'],['qemu-arm'])]
legacy_cases=['check','check-oem-mode','world-writable-source','printing','unknown','guard-refused','nonce-changed','bad-image','backup-symlink',
       'public-base','capture-active','concurrent','port-owned','good-install','restore-original',
       'bad-offset','bad-crc','lost-erase-ack','interrupted','recover-after-failure',
       'recover-live-native','recover-wrong-boot','recover-expired','recover-prepared']

coordinated_cases=['check','guard-refused','nonce-changed','capture-active','concurrent','port-owned','good-install','restore-original','bad-offset','bad-crc','interrupted','recover-after-failure','journal-ack-failure','lifecycle-concurrent','bad-lifecycle','wrong-stage']

def crc(data):
    value=0xffff
    for b in data:
        value^=b
        for _ in range(8):value=(value>>1)^(0x8408 if value&1 else 0)
    return value

def packet(cmd,data):
    return b'\xa5\x5a'+bytes([cmd])+len(data).to_bytes(2,'big')+data+crc(data).to_bytes(2,'big')

with tempfile.TemporaryDirectory(prefix='canvas-real-worker-') as temporary:
    root=Path(temporary);native=root/'elegoo_printer';module=root/'dummy-rfid.so'
    (root/'native.c').write_text('#include <unistd.h>\nint main(void){for(;;)pause();}\n')
    (root/'module.c').write_text('void dummy_fixture_only(void){}\n')
    subprocess.run(['gcc',*common,root/'native.c','-o',native],check=True)
    subprocess.run(['gcc','-fPIC','-shared',root/'module.c','-o',module],check=True)
    guard=root/'capture-check.sh'
    guard.write_text('''#!/bin/sh
[ "$1" = --check ] || exit 2
exec python3 -c '
from pathlib import Path
import os,sys
p=Path(os.environ["CASE_ROOT"])
(p/"guard-called").write_text("yes")
mode=(p/"case").read_text()
if mode=="guard-refused":sys.exit(1)
if mode=="nonce-changed":
    q=p/"channel/ready"; b=bytearray(q.read_bytes()); b[8]^=1; q.write_bytes(b)
print("CHECK PASS: fake fresh Idle/cold capture guard")
'
''');guard.chmod(0o700)
    count=0
    for arch,cc,extra,runner in architectures:
        for coordinated in [False,True]:
            for case in (coordinated_cases if coordinated else legacy_cases):
                folder=root/(arch+'-'+('coordinated-' if coordinated else 'legacy-')+case);folder.mkdir(mode=0o700)
                component=folder/'trial';component.mkdir(mode=0o700)
                channel=folder/'channel';channel.mkdir(mode=0o700)
                (folder/'case').write_text(case)
                original=folder/'stock.bin';original.write_bytes(stock);original.chmod(0o600)
                if case=='check-oem-mode':original.chmod(0o664)
                if case=='world-writable-source':original.chmod(0o666)
                dummy=subprocess.Popen([str(native)],env=dict(os.environ,LD_PRELOAD=str(module)))
                master,slave=pty.openpty();device=os.ttyname(slave)
                if case!='port-owned':os.close(slave);slave=-1
                phase=2 if case=='printing' else 0 if case=='unknown' else 1
                ready=channel/'ready';ready.write_bytes(struct.pack('<8sQQQII',b'CC2RF1\0\0',17,0,0,dummy.pid,phase));ready.chmod(0o600)
                for _ in range(100):
                    if str(module) in Path(f'/proc/{dummy.pid}/maps').read_text():break
                    time.sleep(.01)
                else:raise AssertionError('dummy native library not mapped')
                if case=='bad-image':original.write_bytes(stock[:-4]+b'bad!')
                sentinel=folder/'outside';sentinel.write_bytes(b'KEEP');sentinel.chmod(0o600)
                if case=='backup-symlink':(component/'original-stock-44.bin').symlink_to(sentinel)
                if case=='public-base':component.chmod(0o755)
                if case=='capture-active':(channel/'scan-lock').mkdir(mode=0o700)
                held_lock=None
                if case in ['concurrent','lifecycle-concurrent']:
                    held_lock=os.open(component/('lifecycle.lock' if case=='lifecycle-concurrent' else 'worker.lock'),os.O_RDWR|os.O_CREAT,0o600)
                    fcntl.flock(held_lock,fcntl.LOCK_EX|fcntl.LOCK_NB)
                if case.startswith('recover-') and case!='recover-after-failure':
                    (component/'original-stock-44.bin').write_bytes(stock);(component/'original-stock-44.bin').chmod(0o600)
                    boot=Path('/proc/sys/kernel/random/boot_id').read_text().strip().encode().ljust(40,b'\0')
                    if case=='recover-wrong-boot':boot=b'x'*40
                    # Expiry must also be testable immediately after Docker/WSL
                    # starts. A test-only linked clock advances this worker by
                    # 901 seconds; the ticket itself retains a valid positive time.
                    stamp=int(time.monotonic()*1000)
                    stage=1 if case=='recover-prepared' else 3
                    (component/'recovery-ticket').write_bytes(struct.pack('<16s40sQIIQ',b'CC2-CANVAS-R1',boot,stamp,dummy.pid,stage,17))
                    (component/'recovery-ticket').chmod(0o600)
                    if case!='recover-live-native':dummy.terminate();dummy.wait(timeout=3)
                defines={'CV_BASE':str(component),'CV_ORIGINAL':str(original),'CV_CHANNEL':str(channel),
                         'CV_MODULE':str(module),'CV_MODULE_SHA':sha(module),'CV_EXE_SHA':sha(native),
                         'CV_GUARD':str(guard),'CV_GUARD_SHA':sha(guard),'CV_TTY':device}
                if coordinated:defines['CV_COORDINATED']=1
                binary=folder/'worker'
                clock_fixture=[HERE/'test_expiry_clock.c','-Wl,--wrap=clock_gettime'] if case=='recover-expired' else []
                ack_fixture=[]
                if case=='journal-ack-failure':
                    fault=folder/'journal-fault.c'
                    fault.write_text('#include <errno.h>\n#include <string.h>\nint __real_renameat(int,const char*,int,const char*);\nint __wrap_renameat(int a,const char*b,int c,const char*d){static unsigned n;if(!strcmp(d,"lifecycle") && ++n==2){errno=EIO;return -1;}return __real_renameat(a,b,c,d);}\n')
                    ack_fixture=[fault,'-Wl,--wrap=renameat']
                subprocess.run([cc,*common,*extra,*[f'-D{k}={json.dumps(v)}' for k,v in defines.items()],src/'canvas_worker.c',*clock_fixture,*ack_fixture,'-o',binary],check=True)
                recovery=folder/'boot-recover'
                if coordinated:
                    subprocess.run([cc,*common,*extra,'-DCL_BASE='+json.dumps(str(component)),'-DCL_TTY='+json.dumps(device),HERE/'recover_worker.c','-o',recovery],check=True)
                    roles=[]
                    for role in ['bootstrap','native','guard','entry','recover','marker']:
                        target={'native':native,'guard':module,'recover':recovery}.get(role,folder/role)
                        if role not in ['native','guard','recover']:target.write_bytes(('FIXTURE '+role).encode());target.chmod(0o600)
                        roles.append(f'{role} {sha(target)} {target}\n')
                    profile=''.join(roles).encode();(component/'recovery-profile').write_bytes(profile);(component/'recovery-profile').chmod(0o600)
                    phase=5 if case=='restore-original' else 1 if case=='wrong-stage' else 2
                    seed(component,phase,profile)
                    if case=='bad-lifecycle':
                        p=component/'lifecycle';data=bytearray(p.read_bytes());data[0]^=1;p.write_bytes(data)
                frames=[];errors=[];stop=threading.Event();responsive=threading.Event()
                memory=bytearray(b'\x69'*0x4000+patched+b'\xff'*(53248-len(stock)))
                expected_boot=bytes(memory[:0x4000]);runs=[]
                def peer():
                    data=b'';entered=False;pings=0;erases=0;offset=0;injected=False
                    end=time.monotonic()+35
                    while not stop.is_set() and time.monotonic()<end:
                        if not select.select([master],[],[],.02)[0]:continue
                        try:chunk=os.read(master,4096)
                        except OSError as error:
                            if error.errno==errno.EIO:time.sleep(.01);continue
                            errors.append(str(error));return
                        if not chunk:continue
                        data+=chunk
                        if not entered:
                            magic=b' \x1c Request Serial Bootloader!! ~'
                            if len(data)<len(magic):continue
                            if not data.startswith(magic):errors.append('bad boot-entry bytes');return
                            data=data[len(magic):];entered=True
                        while len(data)>=5:
                            if data[:2]!=b'\xa5\x5a':errors.append('bad packet prefix');return
                            n=int.from_bytes(data[3:5],'big')
                            if n>1036:errors.append('unsafe packet size');return
                            if len(data)<n+7:break
                            frame,data=data[:n+7],data[n+7:];cmd=frame[2];payload=frame[5:5+n]
                            if crc(payload)!=int.from_bytes(frame[-2:],'big'):errors.append('bad request CRC');return
                            frames.append((cmd,payload))
                            if cmd==3:
                                pings+=1
                                if case=='recover-after-failure' and injected and not responsive.is_set():continue
                                os.write(master,packet(3,payload));continue
                            if cmd==2:
                                if offset!=len(stock):errors.append('jump into incomplete application');return
                                runs.append(bytes(memory[0x4000:0x4000+len(stock)]));continue
                            if cmd==0:
                                if payload!=struct.pack('<I',len(stock)):errors.append('non-application erase size');return
                                erases+=1;offset=0;memory[0x4000:]=b'\xff'*53248
                                if case=='lost-erase-ack' and not injected:injected=True;continue
                                os.write(master,packet(0,struct.pack('<I',53248)));continue
                            if cmd!=1 or n<16:errors.append('unknown command');return
                            off,length,total=struct.unpack_from('<III',payload)
                            if off!=offset or length<4 or length>1024 or length%4 or total!=len(stock) or off+length>len(stock) or n!=length+12:
                                errors.append('invalid application write bounds');return
                            memory[0x4000+off:0x4000+off+length]=payload[12:];offset+=length
                            ack=struct.pack('<II',length,offset)
                            if offset==10240 and not injected and case in ['bad-offset','bad-crc','interrupted','recover-after-failure','journal-ack-failure']:
                                injected=True
                                if case in ['bad-offset','recover-after-failure']:ack=struct.pack('<II',length,offset-4)
                                if case=='interrupted':os.kill(active.pid,signal.SIGTERM)
                                reply=packet(1,ack)
                                if case=='bad-crc':reply=reply[:-1]+bytes([reply[-1]^1])
                                os.write(master,reply);continue
                            os.write(master,packet(1,ack))
                thread=threading.Thread(target=peer,daemon=True);thread.start()
                try:
                    action='--check' if case in ['check','check-oem-mode'] else '--restore-original' if case=='restore-original' else '--recover-original' if case.startswith('recover-') and case!='recover-after-failure' else '--install-filter'
                    active=subprocess.Popen([*runner,str(binary),action],stdout=subprocess.PIPE,stderr=subprocess.PIPE,text=True,env=dict(os.environ,CASE_ROOT=str(folder)))
                    stdout,stderr=active.communicate(timeout=30)
                    if case in ['recover-after-failure','journal-ack-failure']:
                        assert active.returncode!=0 and ('lifecycle_completion_not_durable_native_boot_blocked' if case=='journal-ack-failure' else 'application_incomplete_recovery_required_do_not_reboot') in stderr,(arch,case,stdout,stderr)
                        assert not runs
                        # Legacy recovery uses its same-boot ticket. Coordinated
                        # recovery uses the real durable lifecycle boot worker.
                        responsive.set()
                        active=subprocess.Popen([*runner,str(recovery if coordinated else binary),'--recover-original'],stdout=subprocess.PIPE,stderr=subprocess.PIPE,text=True,env=dict(os.environ,CASE_ROOT=str(folder)))
                        stdout,stderr=active.communicate(timeout=20)
                    time.sleep(.05)
                    success=case in ['check','check-oem-mode','good-install','restore-original','recover-after-failure','journal-ack-failure']
                    assert (active.returncode==0)==success,(arch,case,active.returncode,stdout,stderr,errors)
                    if case in ['good-install','restore-original','bad-offset','bad-crc','lost-erase-ack','interrupted','recover-after-failure','journal-ack-failure','port-owned']:
                        assert dummy.poll() is not None,(arch,case,'native not stopped')
                    elif not case.startswith('recover-'):
                        assert dummy.poll() is None,(arch,case,'native stopped before refusal')
                    if case=='good-install':assert runs==[patched],(arch,case,stdout,stderr)
                    elif case in ['restore-original','bad-offset','bad-crc','lost-erase-ack','interrupted','recover-after-failure','journal-ack-failure']:
                        assert runs==[stock],(arch,case,stdout,stderr)
                    else:assert not frames,(arch,case,frames)
                    assert bytes(memory[:0x4000])==expected_boot and sentinel.read_bytes()==b'KEEP'
                    assert not errors,(arch,case,errors)
                    if runs and coordinated:
                        assert (component/'lifecycle').read_bytes()[16]==(4 if case=='good-install' else 7)
                        assert (component/'original-stock-44.bin').read_bytes()==stock
                        if case=='journal-ack-failure':assert list(component.glob('.lifecycle-*.next'))
                    if coordinated:
                        assert not (component/'recovery-ticket').exists(), 'coordinated mode must have one durable authority'
                        if not runs:assert (component/'lifecycle').read_bytes()[16]==(3 if case=='port-owned' else phase)
                    if runs and not coordinated:
                        ticket=(component/'recovery-ticket').read_bytes()
                        assert len(ticket)==80
                        assert struct.unpack('<16s40sQIIQ',ticket)[4]==(4 if case=='good-install' else 5)
                        assert (component/'original-stock-44.bin').read_bytes()==stock
                    if case=='check-oem-mode':
                        backup=component/'original-stock-44.bin'
                        assert backup.read_bytes()==stock and backup.stat().st_mode&0o777==0o600
                        assert original.stat().st_mode&0o777==0o664
                    if case=='world-writable-source':assert not (component/'original-stock-44.bin').exists()
                    print('PASS:',arch,'coordinated' if coordinated else 'legacy',case,'actual worker; dummy native/PTY/memory flash only',flush=True);count+=1
                finally:
                    stop.set();thread.join(timeout=2);os.close(master)
                    if slave>=0:os.close(slave)
                    if held_lock is not None:os.close(held_lock)
                    if dummy.poll() is None:dummy.kill()
                    dummy.wait(timeout=3)
    assert count==2*(len(legacy_cases)+len(coordinated_cases))
    print(f'PASS: {count} actual legacy/coordinated transfer cases; durable intent/ACK, invalid-stage refusal and separate boot recovery after journal failure. No real device accessed.',flush=True)
