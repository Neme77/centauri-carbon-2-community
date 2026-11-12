"""Exercise the actual private manager on isolated files and fake HTTP replies."""
from pathlib import Path
import hashlib,json,os,shutil,stat,subprocess,tempfile
from prepare_payload import prepare,qualified_network_entry

repo=Path(__file__).resolve().parents[2]
def digest(data):return hashlib.sha256(data).hexdigest()
def write(path,data,mode=0o600):
    path.parent.mkdir(parents=True,exist_ok=True);path.write_bytes(data);path.chmod(mode)
def manifest(directory,prefix=''):
    files=sorted(p for p in directory.rglob('*') if p.is_file() and p.name!='SHA256SUMS')
    write(directory/'SHA256SUMS',''.join(digest(p.read_bytes())+'  '+prefix+p.relative_to(directory).as_posix()+'\n' for p in files).encode())
def snapshot(files):return {str(p):(p.read_bytes(),stat.S_IMODE(p.stat().st_mode)) for p in files}

cases=['installer-busy','installer-wrong-barrier','installer-tampered-guard','installer-hot','installer-calibration','installer-busy-foreign','check','success','reactor-dot','network-bare','duplicate-active-entry','without-network','copy-failure','cancelled-idle','minimal-machine','hot','heating','printing','stale','network-pending','console-busy','cell-owned','nozzle-active','foreign-launcher','damaged-reactor','foreign-profile','symlink-launcher','hardlink-launcher','damaged-payload','foreign-after-enable','damaged-disable']
for shell in [['dash'],['busybox','sh']]:
    for case in cases:
        with tempfile.TemporaryDirectory(prefix='cavity-manager-') as temporary:
            root=Path(temporary);base=root/'component';runtime=root/'runtime';network=root/'network';fake=root/'bin'
            runtime.mkdir();fake.mkdir()
            module=root/'dummy.so';module.write_bytes(b'dummy library; never executed\n')
            prepare(repo,module,base)
            native=b'dummy native; never executed\n';reactor=b'dummy existing reactor; never executed\n'
            write(runtime/'elegoo_printer',native,0o700);write(runtime/'libcc2-reactor-v2.so',reactor,0o700)
            write(runtime/'launcher.sh',(repo/'experimental/firmware-port/launcher.sh').read_bytes(),0o700);manifest(runtime,'./' if case=='reactor-dot' else '')
            files=[runtime/'launcher.sh',runtime/'SHA256SUMS']
            if case!='without-network':
                network.mkdir();write(network/'profile.sh',(repo/'experimental/network-connections/profile.sh').read_bytes(),0o755)
                write(network/'reactor-entry.sh',qualified_network_entry((repo/'experimental/firmware-port/launcher.sh').read_bytes()),0o700)
                write(network/'activate.sh',(repo/'experimental/network-connections/activate.sh').read_bytes(),0o700)
                write(network/'source/connections/profile.sh',(network/'profile.sh').read_bytes(),0o644)
                write(network/'source/connections/activate.sh',(network/'activate.sh').read_bytes(),0o644)
                manifest(network,'' if case=='network-bare' else './')
                if case=='duplicate-active-entry':
                    with (network/'SHA256SUMS').open('ab') as out:out.write((digest((network/'profile.sh').read_bytes())+'  profile.sh\n').encode())
                files += [network/'profile.sh',network/'reactor-entry.sh',network/'activate.sh',network/'SHA256SUMS']
            before=snapshot(files)
            # Test-only override reproduces an archived installer against the
            # same reduced userland; production activation has no such option.
            manager_source=Path(os.environ['CC2_TEST_MANAGER_FILE']) if os.environ.get('CC2_TEST_MANAGER_FILE') else base/'manage.sh'
            text=manager_source.read_text()
            for old,new in [('/opt/usr/cc2-cavity-floor',str(base)),('/opt/usr/cc2-reactor-runtime-v2',str(runtime)),('/opt/usr/cc2-network-manager',str(network))]:text=text.replace(old,new)
            text=text.replace('19712b5943b10bb8cb6aab9a4ed498ee962b28538b23c91b7ed003eb9e590745',digest(native))
            text=text.replace('4639364c68489ec40c4e0c4f425550f3aa19e44c2ee61ed641affdc560e7e92c',digest(reactor))
            text=text.replace('PATH=/usr/sbin:/usr/bin:/sbin:/bin',f'PATH={fake}:/usr/sbin:/usr/bin:/sbin:/bin')
            write(base/'manage.sh',text.encode(),0o700)
            installer_case=case.startswith('installer-')
            shared=root/'shared-installer';borrowed=-1
            if installer_case:
                shared.mkdir(mode=0o700)
                guard=base/'installer-barrier'
                subprocess.run(['gcc','-O2','-std=c11','-Wall','-Wextra','-Wpedantic','-Werror',
                    '-DNB_BARRIER='+json.dumps(str(shared)),repo/'experimental/cavity-fan-floor/installer_barrier.c','-o',guard],check=True,timeout=60)
                guard.chmod(0o700)
                text=text.replace('INSTALLER_GUARD_SHA=\n','INSTALLER_GUARD_SHA='+digest(guard.read_bytes())+'\n',1)
                write(base/'manage.sh',text.encode(),0o700)
                opened=shared
                if case=='installer-wrong-barrier':opened=root/'foreign';opened.mkdir(mode=0o700)
                borrowed=os.open(opened,os.O_RDONLY|os.O_DIRECTORY)
                if case=='installer-tampered-guard':write(guard,b'changed guard',0o700)
            printer={'connected':True,'last_message_age':0,'extruder':{'temperature':25,'target':0},'heater_bed':{'temperature':25,'target':0},'machine':{'status':1,'status_name':'Idle','sub_status':0,'reason':0,'progress':0},'print':{'enabled':False},'bed_drying':{'active':False}}
            replies={'/api/printer':printer,'/api/uds':{'connected':True,'fresh':True,'print_state':'standby'},'/api/app-update/status':{'busy':False},'/api/network/manager':{'busy':False,'pending':None},'/api/calibration-tools/status':{'busy':False,'cell':{'owned':False}},'/api/console':{'busy':False},'/api/nozzle-expansion':{'state':'idle'}}
            if case=='minimal-machine':printer['machine']={'status':1}
            if case=='cancelled-idle':
                printer['print']={'enabled':True,'state':'cancelled'};replies['/api/uds']['print_state']='cancelled'
            if case in ['hot','installer-hot']:printer['extruder']['temperature']=51
            if case=='heating':printer['extruder']['target']=200
            if case=='printing':printer['machine']['status']=2
            if case=='stale':printer['last_message_age']=6
            if case=='network-pending':replies['/api/network/manager']['pending']={'id':1}
            if case in ['console-busy','installer-busy-foreign']:replies['/api/console']['busy']=True
            if case in ['cell-owned','installer-calibration']:replies['/api/calibration-tools/status']['cell']['owned']=True
            if case=='nozzle-active':replies['/api/nozzle-expansion']['state']='running'
            if installer_case:replies['/api/app-update/status']['busy']=True
            write(root/'replies.json',json.dumps(replies,separators=(',',':')).encode())
            write(fake/'wget',('''#!/usr/bin/python3
import json,sys
from pathlib import Path
replies=json.loads(Path('''+repr(str(root/'replies.json'))+''').read_text())
print(json.dumps(replies[sys.argv[-1].replace('http://127.0.0.1:8081','')],separators=(',',':')))
''').encode(),0o700)
            write(fake/'cp',('''#!/usr/bin/python3
import os,sys
from pathlib import Path
counter=Path('''+repr(str(root/'copy-counter'))+''')
if os.environ.get('FAIL_COPY') and '.cc2-cavity-' in sys.argv[-1]:
    n=int(counter.read_text())+1 if counter.exists() else 1
    counter.write_text(str(n))
    if n==2:sys.exit(1)
os.execv('/bin/cp',['cp',*sys.argv[1:]])
''').encode(),0o700)
            write(fake/'sleep',b'#!/bin/sh\nexec /bin/sleep 0.05\n',0o700)
            # Reproduce the printer's missing utility even on a full Linux host.
            write(fake/'stat',('#!/bin/sh\necho attempted >"'+str(root/'stat-called')+'"\nexit 127\n').encode(),0o700)
            if shell[0]=='busybox':
                write(fake/'ls',('#!/bin/sh\nexec '+shutil.which('busybox')+' ls "$@"\n').encode(),0o700)
            if case=='foreign-launcher':write(runtime/'launcher.sh',b'foreign\n',0o700)
            if case=='damaged-reactor':write(runtime/'libcc2-reactor-v2.so',b'damaged\n',0o700)
            if case=='foreign-profile':write(network/'profile.sh',b'foreign\n',0o755)
            if case=='symlink-launcher':
                (runtime/'launcher.sh').rename(root/'launcher-original');(runtime/'launcher.sh').symlink_to(root/'launcher-original')
            if case=='hardlink-launcher':os.link(runtime/'launcher.sh',root/'launcher-link')
            manifest(base)
            if case=='damaged-payload':write(base/'libcc2-cavity-floor-r1.so',b'damaged\n')
            def invoke(action,okay=True,env=None):
                args=[action]
                if installer_case and action in ['enable','check']:
                    args=[action+'-from-installer',str(borrowed)]
                result=subprocess.run([*shell,str(base/'manage.sh'),*args],text=True,capture_output=True,timeout=30,env=env,
                    pass_fds=(borrowed,) if borrowed>=0 else ())
                assert (result.returncode==0)==okay,(shell,case,action,result.returncode,result.stdout,result.stderr)
                return result
            for name in ['manage.sh','launcher.sh','profile.sh']:subprocess.run([*shell,'-n',base/name],check=True)
            refused=case in ['installer-wrong-barrier','installer-tampered-guard','installer-hot','installer-calibration','installer-busy-foreign','duplicate-active-entry','minimal-machine','hot','heating','printing','stale','network-pending','console-busy','cell-owned','nozzle-active','foreign-launcher','damaged-reactor','foreign-profile','symlink-launcher','hardlink-launcher','damaged-payload']
            if refused:
                current=snapshot(files)
                invoke('enable',False);assert snapshot(files)==current
                assert not (base/'enabled').exists()
            elif case=='copy-failure':
                invoke('enable',False,dict(os.environ,FAIL_COPY='1'));assert snapshot(files)==before
                assert not (base/'enabled').exists()
            elif case=='check':invoke('check');assert snapshot(files)==before;assert not (base/'enabled').exists()
            else:
                invoke('enable');invoke('enable');assert (base/'enabled').read_text()=='CC2-CAVITY-FLOOR-R1\n'
                subprocess.run(['sha256sum','-c','SHA256SUMS'],cwd=runtime,check=True,stdout=subprocess.DEVNULL)
                if network.exists():subprocess.run(['sha256sum','-c','SHA256SUMS'],cwd=network,check=True,stdout=subprocess.DEVNULL)
                for file in files:
                    if file.name=='SHA256SUMS':
                        old=before[str(file)][0].splitlines(keepends=True);new=file.read_bytes().splitlines(keepends=True)
                        assert len(old)==len(new)
                        changed=[i for i,(a,b) in enumerate(zip(old,new)) if a!=b]
                        assert len(changed)==(3 if file.parent==network else 1)
                        for i in changed:assert old[i][64:]==new[i][64:],'Manifest path/spacing changed'
                if network.exists():
                    assert (network/'source/connections/profile.sh').read_bytes()==(repo/'experimental/network-connections/profile.sh').read_bytes()
                if case=='foreign-after-enable':
                    write(runtime/'launcher.sh',b'foreign\n',0o700);current=snapshot(files);invoke('disable',False);assert snapshot(files)==current
                else:
                    if case=='damaged-disable':write(base/'libcc2-cavity-floor-r1.so',b'damaged\n')
                    if installer_case:
                        replies['/api/app-update/status']['busy']=False
                        write(root/'replies.json',json.dumps(replies,separators=(',',':')).encode())
                    invoke('disable');assert snapshot(files)==before;assert not (base/'enabled').exists()
            if borrowed>=0:os.close(borrowed)
            assert not (root/'stat-called').exists(),'The installer called the absent stat utility'
            print('PASS',shell,case,'stat unavailable',flush=True)
print('PASS: actual private manager on dash/BusyBox, activation plus exact permissions/content rollback, LAN profile/manifest compatibility, no-LAN path, guards, foreign edits, symlinks, copy failure and damaged-payload disable. Only fake HTTP/files used.',flush=True)
