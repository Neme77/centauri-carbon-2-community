"""Run the actual old/new managers and migration on isolated, inert files."""
from pathlib import Path
import hashlib,json,os,shutil,stat,subprocess,tempfile,zipfile

repo=Path(__file__).resolve().parents[2]
inputs=Path('/input')
payload=inputs/'boot-fix-payload-r2'
def sha(data):return hashlib.sha256(data).hexdigest()
def write(p,data,mode=0o600):
    p.parent.mkdir(parents=True,exist_ok=True);p.write_bytes(data);p.chmod(mode)
def manifest(p,prefix=''):
    write(p/'SHA256SUMS',''.join(sha(f.read_bytes())+'  '+prefix+f.relative_to(p).as_posix()+'\n' for f in sorted(p.rglob('*')) if f.is_file() and f.name!='SHA256SUMS').encode())
def snapshot(files):return [(p.read_bytes(),stat.S_IMODE(p.stat().st_mode)) for p in files]
with zipfile.ZipFile(inputs/'qualified/CC2-Control-Exhaust-Minimum-Trial-R1b.zip') as z:
    prefix='CC2-Control-Exhaust-Minimum-Trial-R1b/cc2-cavity-floor/'
    old={n[len(prefix):]:z.read(n) for n in z.namelist() if n.startswith(prefix)}
assert sha(old['SHA256SUMS'])=='84b48f37fd2d6f433bb258c4aea0d30ddbd1a5f546ce0367f5081a11c405a7b4'
import sys
sys.path.insert(0,str(repo/'experimental/cavity-fan-floor'))
from prepare_payload import qualified_network_entry
cases=['success','hot','busy','mapped','foreign-component','foreign-entry','foreign-activation','damaged-payload','duplicate-plan','changed-target','new-check-failure','new-enable-failure']
for shell in [['dash'],['busybox','sh']]:
    for case in cases:
        with tempfile.TemporaryDirectory(prefix='cavity-upgrade-') as t:
            root=Path(t);base=root/'component';runtime=root/'runtime';network=root/'network';fake=root/'bin';maps=root/'maps'
            stage=root/('.cc2-cavity-bootfix-'+'a'*32)
            for d in [base,runtime,network,fake,maps,stage]:d.mkdir()
            for name,data in old.items():write(base/name,data,0o700 if name.endswith('.sh') else 0o600)
            shutil.copytree(payload,stage/'payload')
            native=b'inert native';reactor=b'inert reactor'
            write(runtime/'elegoo_printer',native,0o700);write(runtime/'libcc2-reactor-v2.so',reactor,0o700)
            launcher=(repo/'experimental/firmware-port/launcher.sh').read_bytes()
            write(runtime/'launcher.sh',launcher,0o755);manifest(runtime)
            write(network/'profile.sh',(repo/'experimental/network-connections/profile.sh').read_bytes(),0o755)
            write(network/'reactor-entry.sh',qualified_network_entry(launcher),0o700)
            write(network/'activate.sh',(repo/'experimental/network-connections/activate.sh').read_bytes(),0o700)
            write(network/'source/connections/profile.sh',(network/'profile.sh').read_bytes(),0o644)
            manifest(network,'./')
            targets={'reactor-launcher':runtime/'launcher.sh','reactor-manifest':runtime/'SHA256SUMS','network-profile':network/'profile.sh','network-entry':network/'reactor-entry.sh','network-activation':network/'activate.sh','network-manifest':network/'SHA256SUMS'}
            baseline=snapshot(targets.values());backup=base/'backup';backup.mkdir()
            keys=['reactor-launcher','reactor-manifest','network-profile','network-manifest']
            for k in keys:
                shutil.copy2(targets[k],backup/(k+'.old'));shutil.copy2(targets[k],backup/(k+'.new'))
            write(backup/'reactor-launcher.new',old['launcher.sh'],0o755)
            write(backup/'network-profile.new',old['profile.sh'],0o755)
            for key,name,changed in [('reactor-manifest','launcher.sh','reactor-launcher'),('network-manifest','./profile.sh','network-profile')]:
                data=(backup/(key+'.new')).read_bytes()
                a=sha((backup/(changed+'.old')).read_bytes());b=sha((backup/(changed+'.new')).read_bytes())
                write(backup/(key+'.new'),data.replace((a+'  '+name+'\n').encode(),(b+'  '+name+'\n').encode()))
            plan=''.join(sha((backup/(k+'.old')).read_bytes())+' '+sha((backup/(k+'.new')).read_bytes())+' '+k+'\n' for k in keys)
            write(backup/'plan',plan.encode())
            for k in keys:shutil.copy2(backup/(k+'.new'),targets[k])
            write(base/'enabled',b'CC2-CAVITY-FLOOR-R1\n')
            replies={'/api/printer':{'connected':True,'last_message_age':0,'extruder':{'temperature':25,'target':0},'heater_bed':{'temperature':25,'target':0},'machine':{'status':1,'sub_status':0},'bed_drying':{'active':False}},'/api/uds':{'connected':True,'fresh':True,'print_state':'standby'},'/api/app-update/status':{'busy':False},'/api/network/manager':{'busy':False,'pending':None},'/api/calibration-tools/status':{'busy':False,'cell':{'owned':False}},'/api/console':{'busy':False},'/api/nozzle-expansion':{'state':'idle'}}
            if case=='hot':replies['/api/printer']['extruder']['temperature']=51
            if case=='busy':replies['/api/network/manager']['busy']=True
            write(root/'replies.json',json.dumps(replies,separators=(',',':')).encode())
            write(fake/'wget',('#!/usr/bin/python3\nimport json,sys\nfrom pathlib import Path\nr=json.loads(Path('+repr(str(root/'replies.json'))+').read_text());print(json.dumps(r[sys.argv[-1].replace("http://127.0.0.1:8081","")],separators=(",",":")))\n').encode(),0o700)
            write(fake/'sleep',b'#!/bin/sh\nexec /bin/sleep 0.02\n',0o700)
            # Only fixture paths and inert native fingerprints are translated.
            # Both managers remain byte-exact in their checksum-verified payload.
            replacements=[('/opt/usr/cc2-cavity-floor',str(base)),('/opt/usr/cc2-reactor-runtime-v2',str(runtime)),('/opt/usr/cc2-network-manager',str(network)),('19712b5943b10bb8cb6aab9a4ed498ee962b28538b23c91b7ed003eb9e590745',sha(native)),('4639364c68489ec40c4e0c4f425550f3aa19e44c2ee61ed641affdc560e7e92c',sha(reactor)),('PATH=/usr/sbin:/usr/bin:/sbin:/bin','PATH='+str(fake)+':/usr/sbin:/usr/bin:/sbin:/bin')]
            dispatcher='''#!/usr/bin/python3
import sys,subprocess
from pathlib import Path
p=Path(sys.argv[1]); action=sys.argv[2]; text=p.read_text()
new='network-activation)' in text
if new and CASE=='new-check-failure' and action=='check':sys.exit(1)
for a,b in REPLACEMENTS:text=text.replace(a,b)
runner=Path(RUNNER);runner.write_text(text)
code=subprocess.run([*SHELL,str(runner),action]).returncode
if not code and new and CASE=='new-enable-failure' and action=='enable':sys.exit(1)
sys.exit(code)
'''.replace('CASE',repr(case)).replace('REPLACEMENTS',repr(replacements)).replace('RUNNER',repr(str(root/'runner.sh'))).replace('SHELL',repr(shell))
            write(fake/'manager',dispatcher.encode(),0o700)
            if case=='mapped':write(maps/'one',(str(base/'libcc2-cavity-floor-r1.so')+'\n').encode())
            if case=='foreign-component':write(base/'thermal.awk',b'foreign')
            if case=='foreign-entry':write(network/'reactor-entry.sh',b'foreign')
            if case=='foreign-activation':write(network/'activate.sh',b'foreign')
            if case=='damaged-payload':write(stage/'payload/launcher.sh',b'foreign')
            if case=='duplicate-plan':write(backup/'plan',(plan+plan.splitlines()[0]+'\n').encode())
            if case=='changed-target':write(runtime/'launcher.sh',b'foreign')
            before=snapshot(targets.values())
            text=(repo/'experimental/cavity-fan-floor/upgrade_boot.sh').read_text()
            text=text.replace('@PAYLOAD_SHA@',sha((payload/'SHA256SUMS').read_bytes()))
            for a,b in replacements[:3]:text=text.replace(a,b)
            text=text.replace('/opt/usr/',str(root)+'/').replace('/proc/[0-9]*/maps',str(maps/'*'))
            text=text.replace('sh "$BASE/manage.sh"',str(fake/'manager')+' "$BASE/manage.sh"')
            script=stage/'upgrade.sh';write(script,text.encode())
            r=subprocess.run([*shell,script],capture_output=True,text=True,timeout=60)
            assert (r.returncode==0)==(case=='success'),(shell,case,r.stdout,r.stderr)
            if case=='success':
                assert len((base/'backup/plan').read_text().splitlines())==6
                assert (stage/'previous-component/backup/plan').exists()
                assert (base/'libcc2-cavity-floor-r1.so').read_bytes()==old['libcc2-cavity-floor-r1.so']
                for d in [runtime,network]:subprocess.run(['sha256sum','-c','SHA256SUMS'],cwd=d,check=True,stdout=subprocess.DEVNULL)
                subprocess.run([fake/'manager',base/'manage.sh','disable'],check=True,stdout=subprocess.DEVNULL)
                assert snapshot(targets.values())==baseline
            elif case.startswith('new-'):
                assert snapshot(targets.values())==baseline
                assert not (base/'enabled').exists()
                assert (base/'manage.sh').read_bytes()==old['manage.sh']
                assert (stage/'failed-new-component').is_dir()
            else:assert snapshot(targets.values())==before
            assert not (root/'.cc2-cavity-bootfix-lock').exists()
            print('PASS boot migration',shell,case,flush=True)
print('BOOT MIGRATION CHECK PASSED: 24 cases, unchanged library, exact six-target rollback, failed upgrades retain native startup. No printer or vendor executable accessed.',flush=True)
