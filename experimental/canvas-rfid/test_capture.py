"""Real capture CLI + actual R2 producer functions, all on synthetic fixtures."""
from pathlib import Path
import hashlib
import json
import os
import queue
import shutil
import signal
import struct
import subprocess
import tempfile
import threading
import time

base=Path(__file__).resolve().parent;src=base/'host'
sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
common=['-O2','-UNDEBUG','-std=c11','-Wall','-Wextra','-Wpedantic','-Werror','-D_POSIX_C_SOURCE=200809L','-pthread','-I'+str(src)]
architectures=[('host','gcc',[],[]),('arm','arm-linux-gnueabihf-gcc',['-static','-marm','-mcpu=cortex-a7','-mfpu=neon-vfpv4','-mfloat-abi=hard','-D_TIME_BITS=32','-D_FILE_OFFSET_BITS=32'],['qemu-arm'])]
scenarios=['foreign','native','check-only','unknown','printing','guard-hot','guard-stale-then-ok','guard-always-stale',
           'change-state','wrong-token','invalid-owned','timeout','no-native-frames','signal','concurrent','result-symlink','public-result','foreign-lock']
with tempfile.TemporaryDirectory(prefix='cc2-capture-fixtures-') as temporary:
    root=Path(temporary)
    for arch,cc,extra,runner,native_guard in [(a+c,cc,extra,runner,native) for a,cc,extra,runner in architectures for c,native in [('-legacy',0),('-native',1)]]:
        folder=root/arch;folder.mkdir()
        fixture=folder/'fixture';channel=fixture/'channel';logroot=fixture/'capture';module=fixture/'dummy-map.so';mode=fixture/'mode';guard=folder/'guard.sh'
        fixture.mkdir();module.write_bytes(b'x'*4096)
        guard.write_text('''#!/bin/sh
[ \"$1\" = \"${EXPECTED_GUARD_ARGUMENT}\" ] || exit 97
exec python3 -c '
from pathlib import Path
import os,sys
p=Path(os.environ["CASE_ROOT"]); f=p/"guard-count"
n=int(f.read_text()) if f.exists() else 0; f.write_text(str(n+1))
mode=(p/"guard-mode").read_text()
if mode=="hot": print("STOP: printer_not_cold",file=sys.stderr);sys.exit(1)
if mode=="stale" or (mode=="stale-once" and n==0): print("STOP: stale_telemetry",file=sys.stderr);sys.exit(1)
print("CHECK PASS: fake fresh Idle/cold guard")
'
''');guard.chmod(0o700)
        producer=folder/'producer'
        subprocess.run([cc,*common,*extra,f'-DRF_CHANNEL={json.dumps(str(channel))}',base/'test_capture_producer.c',src/'rfid_guard.c','-o',producer],check=True)
        exe_hash=sha(Path(shutil.which('qemu-arm'))) if runner else sha(producer)
        defines={'RF_CHANNEL':str(channel),'CT_MODULE':str(module),'CT_MODULE_SHA':sha(module),'CT_EXE_SHA':exe_hash,'CT_GUARD':str(guard),'CT_GUARD_SHA':sha(guard),'CT_LOGROOT':str(logroot)}
        helper=folder/'capture'
        subprocess.run([cc,*common,*extra,*[f'-D{k}={json.dumps(v)}' for k,v in defines.items()],f'-DCT_GUARD_NATIVE={native_guard}','-DCT_SCAN_SECONDS=3','-DCT_RETRY_MILLISECONDS=10',base/'capture.c','-o',helper],check=True)
        for scenario in scenarios:
            shutil.rmtree(fixture);fixture.mkdir();logroot.mkdir(mode=0o700);module.write_bytes(b'x'*4096)
            producer_mode=scenario if scenario in ['foreign','native','unknown','printing','change-state','wrong-token','invalid-owned','no-native-frames'] else ('foreign' if scenario.startswith('guard-') or scenario=='check-only' else 'none')
            mode.write_text(producer_mode+'\n')
            (fixture/'guard-mode').write_text({'guard-hot':'hot','guard-stale-then-ok':'stale-once','guard-always-stale':'stale'}.get(scenario,'ok'))
            server=subprocess.Popen([*runner,str(producer),str(module),str(mode)],stdout=subprocess.DEVNULL,stderr=subprocess.PIPE,text=True)
            sentinel=None
            env=dict(os.environ,CASE_ROOT=str(fixture),EXPECTED_GUARD_ARGUMENT='--check-active' if native_guard else 'check')
            try:
                for _ in range(200):
                    ready=channel/'ready'
                    if ready.exists() and ready.stat().st_size==40:
                        expected=0 if scenario=='unknown' else (2 if scenario=='printing' else 1)
                        if struct.unpack('<8sQQQII',ready.read_bytes())[-1]==expected:break
                    if server.poll() is not None:raise AssertionError(('producer failed',server.returncode,server.stderr.read()))
                    time.sleep(.01)
                else:raise AssertionError('producer capability unavailable')
                if scenario=='result-symlink':
                    sentinel=fixture/'outside';sentinel.write_bytes(b'z'*232);sentinel.chmod(0o600);(channel/'result').symlink_to(sentinel)
                if scenario=='public-result':(channel/'result').write_bytes(b'z'*232);(channel/'result').chmod(0o644)
                if scenario=='foreign-lock':(channel/'scan-lock').mkdir(mode=0o700);(channel/'scan-lock/keep').write_text('foreign lock')
                action='--check' if scenario=='check-only' else '--scan'
                command=[*runner,str(helper),action]
                if scenario in ['signal','concurrent']:
                    scan=subprocess.Popen(command,env=env,stdout=subprocess.PIPE,stderr=subprocess.PIPE,text=True)
                    lines=queue.Queue()
                    def collect():
                        for line in scan.stdout:lines.put(line)
                    thread=threading.Thread(target=collect,daemon=True);thread.start();seen=[];end=time.monotonic()+6
                    while time.monotonic()<end:
                        line=lines.get(timeout=max(.01,end-time.monotonic()));seen.append(line)
                        if 'SCAN ARMED:' in line:break
                    else:raise AssertionError('no live SCAN ARMED progress')
                    if scenario=='concurrent':
                        second=subprocess.run(command,env=env,capture_output=True,text=True,timeout=8)
                        assert second.returncode==1 and 'capture_session_locked' in second.stderr,(arch,second.stdout,second.stderr)
                    scan.send_signal(signal.SIGINT);scan.wait(timeout=5);thread.join(timeout=1)
                    stderr=scan.stderr.read()
                    assert scan.returncode==1 and 'scan_interrupted' in stderr,(arch,scenario,stderr)
                    result_stdout=''.join(seen)
                    assert not (channel/'scan-lock').exists()
                else:
                    result=subprocess.run(command,env=env,capture_output=True,text=True,timeout=12)
                    success=scenario in ['foreign','native','check-only','guard-stale-then-ok']
                    assert (result.returncode==0)==success,(arch,scenario,result.returncode,result.stdout,result.stderr)
                    result_stdout=result.stdout
                    if scenario in ['foreign','native','guard-stale-then-ok']:
                        line=next(line for line in result.stdout.splitlines() if line.startswith('RFID_RESULT='))
                        decoded=json.loads(line.split('=',1)[1]);expected=bytearray((i*7)&255 for i in range(180))
                        if scenario=='native':expected[64:68]=bytes.fromhex('36eeeeee')
                        assert decoded['uid_hex']==bytes(range(0x11,0x19)).hex()
                        assert decoded['pages_hex']==expected.hex()
                        assert decoded['kind']==('native_elegoo' if scenario=='native' else 'foreign')
                    if scenario in ['wrong-token','timeout','no-native-frames']:assert 'scan_deadline_expired' in result.stderr
                    if scenario in ['timeout','no-native-frames','wrong-token']:
                        diagnostic=json.loads(next(line.split('=',1)[1] for line in result.stdout.splitlines() if line.startswith('RFID_DIAGNOSTIC=')))
                        assert diagnostic['native_request_accepted']==(scenario!='no-native-frames'),(arch,scenario,diagnostic)
                        assert diagnostic['own_request_still_pending']==(scenario=='no-native-frames'),(arch,scenario,diagnostic)
                        assert diagnostic['result_record_seen']==(scenario=='wrong-token'),(arch,scenario,diagnostic)
                        assert diagnostic['valid_owned_result_seen'] is False
                    if scenario=='invalid-owned':assert 'invalid_owned_capture' in result.stderr
                    if scenario=='change-state':assert 'native_state_changed_scan_cancelled' in result.stderr
                    if scenario in ['unknown','printing']:assert 'native_not_idle_capture_disabled' in result.stderr and not (channel/'request').exists()
                    if scenario=='guard-hot':assert int((fixture/'guard-count').read_text())==1
                    if scenario=='guard-stale-then-ok':assert int((fixture/'guard-count').read_text())==2
                    if scenario=='guard-always-stale':assert int((fixture/'guard-count').read_text())==3
                    if scenario in ['check-only','guard-hot','guard-always-stale']:assert not (channel/'request').exists()
                    if scenario=='foreign-lock':assert (channel/'scan-lock/keep').read_text()=='foreign lock'
                    else:assert not (channel/'scan-lock').exists()
                assert server.poll() is None,(arch,scenario,'native producer was stopped')
                if sentinel:assert sentinel.read_bytes()==b'z'*232
                if scenario not in ['foreign','native','guard-stale-then-ok']:assert 'RFID_RESULT=' not in result_stdout
                print('PASS:',arch,scenario,'actual capture CLI and R2 producer; no UART/vendor executable',flush=True)
            finally:
                if server.poll() is None:server.terminate()
                server.wait(timeout=5)
                if server.stderr:server.stderr.close()
    print('PASS: one-shot owned raw data, wrong/stale results, state changes, cancellation, concurrent scans, private file guards and bounded freshness retry.',flush=True)
