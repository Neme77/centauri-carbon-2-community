"""Execute actual preparation shell with inert publisher/retirement/manager."""
from pathlib import Path
import hashlib, os, shutil, subprocess, tempfile
HERE=Path(__file__).resolve().parent
CASES=['check','apply','stale-once','stale-always','other-error','publish-error',
       'retire-error','foreign-lock','changed-source','foreign-floor','existing-floor','publisher-symlink']
sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
def put(p,text,mode=0o700):
    p.parent.mkdir(parents=True,exist_ok=True,mode=0o700)
    p.write_text(text);p.chmod(mode)
def run():
    count=0
    with tempfile.TemporaryDirectory(prefix='cc2-cumulative-order-') as temporary:
        root=Path(temporary)
        for shell in [['dash'],['busybox','sh']]:
            for case in CASES:
                home=root/(shell[0]+'-'+case);stage=home/'stage';floor=home/'installed-floor';lock=home/'barrier';events=home/'events'
                source=stage/'floor';source.mkdir(parents=True,mode=0o700)
                manager=f'''#!/bin/sh
[ -d '{lock}' ] || exit 90
echo "manager:$1" >>'{events}'
n=0;[ ! -f '{home}/attempts' ] || n=$(cat '{home}/attempts');n=$((n+1));echo "$n" >'{home}/attempts'
case '{case}' in
 stale-once) if [ "$n" = 1 ];then echo 'STOP: stale_telemetry';exit 1;fi;;
 stale-always) echo 'STOP: stale_telemetry';exit 1;;
 other-error) echo 'STOP: active_print';exit 1;;
esac
exit 0
'''
                put(source/'manage.sh',manager)
                put(source/'SHA256SUMS',sha(source/'manage.sh')+'  manage.sh\n',0o600)
                payload_sha=sha(source/'SHA256SUMS')
                put(stage/'retire_completed.sh',f'#!/bin/sh\necho "retire:$1" >>\'{events}\'\n'+('exit 1\n' if case=='retire-error' else 'exit 0\n'))
                publisher=f'''#!/bin/sh
[ -d '{lock}' ] || exit 90
echo publish >>'{events}'
'''
                publisher+= 'exit 1\n' if case=='publish-error' else f"mkdir '{floor}' && chmod 700 '{floor}' && cp -p '{source}/manage.sh' '{source}/SHA256SUMS' '{floor}/'\n"
                # Reproduce the actual Windows/SCP 0600 helper transfer.
                put(stage/'floor-publisher',publisher,0o600)
                text=(HERE/'prepare-native.sh').read_text().replace('FLOOR=/opt/usr/cc2-cavity-floor','FLOOR='+str(floor)).replace('LOCK=/tmp/cc2-control-install.lock','LOCK='+str(lock)).replace('7a08e95b2e92dcd93b7cdba4353001f71927f3f2955343205c607553981afa28',payload_sha)
                put(stage/'prepare-native.sh',text)
                rows=''.join(sha(p)+'  '+p.relative_to(stage).as_posix()+'\n' for p in sorted(stage.rglob('*')) if p.is_file())
                put(stage/'SHA256SUMS',rows,0o600)
                if case=='publisher-symlink':
                    saved=home/'foreign-helper';(stage/'floor-publisher').rename(saved)
                    (stage/'floor-publisher').symlink_to(saved)
                if case=='changed-source':put(source/'manage.sh',manager+'# changed\n')
                if case=='existing-floor':shutil.copytree(source,floor);floor.chmod(0o700)
                if case=='foreign-floor':
                    floor.mkdir(mode=0o700);put(floor/'SHA256SUMS','preserve\n',0o600)
                if case=='foreign-lock':lock.mkdir(mode=0o700);put(lock/'foreign','preserve\n',0o600)
                bin_dir=home/'bin';put(bin_dir/'sleep','#!/bin/sh\nexit 0\n')
                action='--check' if case=='check' else '--apply'
                result=subprocess.run([*shell,stage/'prepare-native.sh',action],capture_output=True,text=True,
                    env=dict(os.environ,PATH=str(bin_dir)+':'+os.environ['PATH']),timeout=10)
                okay=case in ['check','apply','stale-once','existing-floor']
                assert (result.returncode==0)==okay,(shell,case,result.stdout,result.stderr)
                lines=events.read_text().splitlines() if events.exists() else []
                if case in ['changed-source','foreign-floor','publisher-symlink']:assert not lines,(case,lines)
                if case=='publisher-symlink':assert (home/'foreign-helper').stat().st_mode&0o777==0o600
                if case=='retire-error':assert lines==['retire:--apply'] and not floor.exists()
                if case=='publish-error':assert lines==['retire:--apply','publish']
                if case=='check':assert lines==['retire:--check','publish','manager:check']
                if case=='apply':assert lines==['retire:--apply','publish','manager:enable']
                if case=='existing-floor':assert lines==['retire:--apply','manager:enable']
                if case=='stale-once':assert lines.count('manager:enable')==2
                if case=='stale-always':assert lines.count('manager:enable')==3
                if case=='other-error':assert lines.count('manager:enable')==1
                if case=='foreign-lock':assert (lock/'foreign').read_text()=='preserve\n' and 'publish' not in lines
                else:assert not lock.exists(),(case,'barrier leaked')
                count+=1
    print(f'PASS: {count} actual cumulative preparation shell cases; inert boundaries, ordered retirement/publication, retained barriers and bounded stale-only retry',flush=True)
    return count
if __name__=='__main__':run()
