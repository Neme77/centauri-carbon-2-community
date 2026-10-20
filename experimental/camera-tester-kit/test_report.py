"""Linux fixtures: diagnostic privacy and no process/network side effects."""
from pathlib import Path
import os,shutil,stat,subprocess,tempfile
HERE=Path(__file__).resolve().parent
SECRET='DO_NOT_EXPORT_PRIVATE_CAMERA_TOKEN'
runners=[['sh']]
if shutil.which('busybox'):runners.append(['busybox','sh'])
count=0
for runner in runners:
    with tempfile.TemporaryDirectory() as directory:
        root=Path(directory);proc=root/'proc';temp=root/'tmp';stubs=root/'stubs'
        for p in [proc/'123/fd',proc/'net',temp,stubs]:p.mkdir(parents=True,exist_ok=True)
        camera=root/'camera';camera.write_text('fixture executable')
        (proc/'123/exe').symlink_to(camera)
        (proc/'123/status').write_text('State:\tS (sleeping)\nVmSize:\t179624 kB\nVmRSS:\t21324 kB\nThreads:\t12\nPrivateField: '+SECRET+'\n')
        (proc/'123/maps').write_text('libcc2-camera-reap-r2.so\nlibcc2-camera-recovery-r2.so\n'+SECRET+'\n')
        (proc/'meminfo').write_text('MemTotal: 111168 kB\nMemAvailable: 33000 kB\nPrivateField: '+SECRET+'\n')
        (proc/'net/unix').write_text('Num RefCount Protocol Flags Type St Inode Path\na: 00000002 00000000 00010000 0001 01 10 /tmp/aicamera_uds\nb: 00000003 00000000 00000000 0001 03 11 /tmp/aicamera_uds\nc: 00000003 00000000 00000000 0001 03 12 /tmp/aicamera_uds\n')
        reap=temp/'cc2-camera-reap.status';recovery=temp/'cc2-camera-recovery.status'
        reap.write_text('trial=R2 pid=123 patched=1 spawned=7 reaped=6 retained=1 token='+SECRET+'\n')
        recovery.write_text('trial=RECOVERY_R2 pid=123 ready=1 announced=2 failed=0 initial_calls=1 initial_returns=1 connections=2 token='+SECRET+' trial=R2='+SECRET+'\n')
        (temp/'ai_camera-trial.log').write_text(SECRET)
        text=(HERE/'cc2-camera-tester-report.sh').read_text()
        text=text.replace('/proc/',str(proc)+'/').replace('/tmp/cc2-camera-report-',str(temp)+'/cc2-camera-report-')
        for name in ['cc2-camera-reap.status','cc2-camera-recovery.status']:
            text=text.replace('/tmp/'+name,str(temp/name))
        script=root/'report.sh';script.write_text(text)
        commands={'pidof':'if [ "$1" = ai_camera ];then printf "%s" "$FIX_CAMERA_PID";else echo 1984;fi',
                  'date':'echo 20261020-220000'}
        for name in ['kill','killall','reboot','wget','curl','ssh','scp']:
            commands[name]='echo FORBIDDEN_ACTION >&2;exit 99'
        for name,body in commands.items():
            p=stubs/name;p.write_text('#!/bin/sh\n'+body+'\n');p.chmod(0o700)
        def run(tester='T1',phase='after-start',pid='123',success=True):
            global count
            before=set(temp.glob('cc2-camera-report-*.txt'))
            env=dict(os.environ,PATH=str(stubs)+':'+os.environ['PATH'],FIX_CAMERA_PID=pid)
            r=subprocess.run([*runner,str(script),tester,phase],env=env,capture_output=True,text=True,timeout=10)
            assert (r.returncode==0)==success,(r.stdout,r.stderr)
            assert SECRET not in r.stdout+r.stderr and 'FORBIDDEN_ACTION' not in r.stdout+r.stderr
            after=set(temp.glob('cc2-camera-report-*.txt'))
            if success:
                assert len(after-before)==1
                report=(after-before).pop();data=report.read_text()
                assert SECRET not in data and stat.S_IMODE(report.stat().st_mode)==0o600
            else:assert after==before
            count+=1
            return r.stdout
        output=run()
        assert 'announced=2' in output and 'spawned=7 reaped=6' in output
        assert 'listening=1 connected=2' in output and 'PrivateField' not in output
        recovery.write_text('trial=RECOVERY_R2 pid=999 token='+SECRET+'\n')
        assert 'marker=missing_or_stale' in run()
        reap.unlink();assert 'marker=missing' in run()
        assert 'camera=missing_or_multiple' in run(pid='')
        assert 'camera=missing_or_multiple' in run(pid='123 124')
        (proc/'123/maps').unlink();(proc/'123/status').unlink()
        assert 'libcc2-camera-reap-r2.so=not_mapped' in run()
        for value in ['../escape','T1;reboot','x'*33,'bad\nvalue']:
            run(tester=value,success=False)
        run(phase='../escape',success=False)
print(f'PASS: {count} report privacy/read-only fixtures; shells: {runners}')
