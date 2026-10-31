"""Exercise exact Unix launcher with GNU and macOS-style checksum tools."""
from pathlib import Path
import hashlib,os,shutil,subprocess,tempfile
root=Path(__file__).resolve().parent
for checksum in ('sha256sum','shasum'):
    assert shutil.which(checksum)
    for scenario in ('success','bad-ip','bad-checksum','ssh-failure'):
        with tempfile.TemporaryDirectory(prefix='connections-unix-') as tmp:
            base=Path(tmp);stage=base/'Folder with spaces';stage.mkdir();bin=base/'bin';bin.mkdir()
            script=stage/'install.sh';script.write_bytes((root/'unix-install.sh.in').read_bytes())
            names=('prepare-disk.sh','cc2-control-payload.tar.gz','cc2-connections.tar.gz','run-install.sh','verify-and-run.sh')
            for name in names:(stage/name).write_bytes(b'fixture')
            (stage/'SHA256SUMS').write_text(''.join(f'{hashlib.sha256((stage/name).read_bytes()).hexdigest()}  {name}\n' for name in names))
            for tool in (checksum,'dirname'):(bin/tool).symlink_to(shutil.which(tool))
            for tool in ('ssh','scp'):
                p=bin/tool;p.write_text('#!/bin/sh\nprintf "%s\\n" "'+tool+'" "$@" >> "$CC2_ARGV_LOG"\n'+('exit 9\n' if scenario=='ssh-failure' and tool=='ssh' else 'exit 0\n'));p.chmod(0o700)
            if scenario=='bad-checksum':(stage/'cc2-connections.tar.gz').write_bytes(b'changed')
            log=base/'transport.log';env=dict(os.environ,PATH=str(bin),CC2_ARGV_LOG=str(log))
            ip=b'192.0.2.103;reboot\n' if scenario=='bad-ip' else b'192.0.2.103\n'
            result=subprocess.run(['/bin/sh',script],input=ip,env=env,capture_output=True,timeout=15)
            if scenario=='success':
                assert result.returncode==0,(checksum,result.stdout,result.stderr)
                lines=log.read_text().splitlines();assert lines.count('ssh')==2 and lines.count('scp')==2
                assert 'cc2-connections.tar.gz' in lines
                assert 'sh /opt/usr/cc2-control-connections-r5/verify-and-run.sh' in lines
            else:
                assert result.returncode!=0
                if scenario!='ssh-failure':assert not log.exists()
print('PASS: exact Unix launcher, GNU/macOS checksum selection, spaces, fixed SSH commands, invalid input and failed transfer refusal; no printer accessed')
