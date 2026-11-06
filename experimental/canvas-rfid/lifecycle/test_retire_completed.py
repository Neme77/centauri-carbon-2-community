"""Actual retirement shell, fake old coordinator/API, no native execution."""
from pathlib import Path
import hashlib,json,os,shutil,subprocess,tempfile
HERE=Path(__file__).resolve().parent
CASES=['check','apply','absent','foreign-coordinator','coordinator-refused','active','busy',
       'reboot','recovery','metadata-invalid','cache-changed','foreign-lock','public-root','symlink-root','move-failed','http-timeout']
def put(p,data,mode=0o600):
    p.parent.mkdir(parents=True,exist_ok=True,mode=0o700);p.write_text(data);p.chmod(mode)
def run():
    count=0
    with tempfile.TemporaryDirectory(prefix='cumulative-retire-') as temporary:
        root=Path(temporary)
        for shell in [['dash'],['busybox','sh']]:
            for case in CASES:
                f=root/(shell[0]+'-'+case);parent=f/'parent';parent.mkdir(parents=True,mode=0o700)
                base=parent/'cc2-rfid';base.mkdir(mode=0o700);lock=f/'install.lock';cmd=f/'bin';cmd.mkdir()
                coordinator='#!/bin/sh\n[ "$1" = --verify-disabled-boot ] || exit 2\nexit '+('1' if case=='coordinator-refused' else '0')+'\n'
                put(base/'coordinator',coordinator,0o700)
                digest=hashlib.sha256(coordinator.encode()).hexdigest()
                good={'installed':True,'metadata_valid':True,'phase':10,'generation':10,'busy':False,
                      'reboot_required':False,'recovery_required':False,'result':0,'activation_requested':False}
                if case=='active':good['phase']=5
                if case=='busy':good['busy']=True
                if case=='reboot':good['reboot_required']=True
                if case=='recovery':good['recovery_required']=True
                if case=='metadata-invalid':good['metadata_valid']=False
                response=json.dumps(good,separators=(',',':'))
                changed=json.dumps(dict(good,phase=1),separators=(',',':'))
                if case=='http-timeout':wget='#!/bin/sh\nexec /bin/sleep 30\n'
                elif case=='cache-changed':wget=f'#!/bin/sh\nif [ -f "{f}/read" ];then echo \'{changed}\';else touch "{f}/read";echo \'{response}\';fi\n'
                else:wget=f'#!/bin/sh\necho \'{response}\'\n'
                put(cmd/'wget',wget,0o700);put(cmd/'sleep','#!/bin/sh\nexec /bin/sleep 0.02\n',0o700)
                if case=='move-failed':put(cmd/'mv','#!/bin/sh\nexit 1\n',0o700)
                if case=='foreign-lock':lock.mkdir(mode=0o700);put(lock/'foreign','preserve')
                if case=='public-root':base.chmod(0o755)
                if case=='foreign-coordinator':put(base/'coordinator',coordinator+'# changed\n',0o700)
                if case=='symlink-root':
                    saved=parent/'saved';base.rename(saved);base.symlink_to(saved,target_is_directory=True)
                if case=='absent':shutil.rmtree(base)
                script=(HERE/'retire_completed.sh').read_text().replace('BASE=/opt/usr/cc2-rfid','BASE='+str(base)).replace('PARENT=/opt/usr','PARENT='+str(parent)).replace('LOCK=/tmp/cc2-control-install.lock','LOCK='+str(lock)).replace('add9d432ebd4f91650eccb6b42f92aa172c0d972d451fbace65cbed1467faf1d',digest)
                path=f/'retire.sh';put(path,script,0o700)
                action='--check' if case=='check' else '--apply'
                result=subprocess.run([*shell,path,action],env=dict(os.environ,PATH=str(cmd)+':'+os.environ['PATH']),capture_output=True,text=True,timeout=12)
                okay=case in ['check','apply','absent']
                assert (result.returncode==0)==okay,(shell,case,result.stdout,result.stderr)
                archives=list(parent.glob('cc2-rfid-retired-*/component'))
                if case=='apply':assert not base.exists() and len(archives)==1 and (archives[0]/'coordinator').read_text()==coordinator
                elif case!='absent':assert base.exists() and not archives,(shell,case)
                if case=='foreign-lock':assert (lock/'foreign').read_text()=='preserve'
                else:assert not lock.exists(),(shell,case,'leaked install barrier')
                count+=1
    print(f'PASS: {count} actual completed-trial retirement shell cases; active/foreign/cache-changed state refused, archive reversible, no native or UART execution')
    return count
if __name__=='__main__':run()
