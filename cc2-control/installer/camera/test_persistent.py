"""Persistent startup transactions and native fallback, without a real printer."""
from pathlib import Path
import hashlib,os,re,shutil,stat,subprocess,tempfile,shlex
HERE=Path(__file__).resolve().parent
NAMES=('common.sh','manage.sh','boot-camera.sh')
IDLE='{"connected":true,"last_message_age":2,"machine":{"status":1,"status_name":"Idle"}}'
sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
CASES=0
SHELL=shlex.split(os.environ.get('CC2_TEST_SHELL','sh'))
def write(path,text,execute=False):
    path.parent.mkdir(parents=True,exist_ok=True);path.write_text(text);path.chmod(0o700 if execute else 0o600)
class Fixture:
    def __init__(self,parent,noop):
        self.root=Path(tempfile.mkdtemp(dir=parent));self.base=self.root/'opt/usr/cc2-control/camera'
        self.source=self.root/'package';self.source.mkdir();self.init=self.root/'etc/init.d/printer'
        self.camera=self.root/'opt/bin/ai_camera';self.loader=self.root/'lib/ld-linux-armhf.so.3'
        self.proc=self.root/'proc/12345';self.proc.mkdir(parents=True);self.stub=self.root/'stubs';self.stub.mkdir()
        self.temp=self.root/'tmp';self.temp.mkdir();self.base.parent.mkdir(parents=True)
        self.old=self.root/'opt/usr/cc2-camera-reap-boot-r1'
        old=f'( if [ -x {self.old}/boot-camera.sh ]; then exec {self.old}/boot-camera.sh; else exec {self.camera}; fi ) & # CC2_CAMERA_BOOT_R1'
        native_profile=os.environ.get('CC2_TEST_NATIVE_PROFILE')=='1'
        if native_profile: old='ai_camera &'
        write(self.init,'#!/bin/sh\nstart() {\n    echo unchanged-gui\n    '+old+'\n    echo unchanged-mqtt\n}\ncase "${1:-}" in start) start;; esac\n',True)
        self.init.chmod(0o755);self.original=self.init.read_bytes()
        write(self.camera,'#!/bin/sh\nprintf \'%s|%s|%s|%s\\n\' "${CC2_CAMERA_REAP_TRIAL-unset}" "${CC2_CAMERA_REAP_ENABLE_LAN-unset}" "${CC2_CAMERA_RECOVERY_TRIAL-unset}" "${LD_PRELOAD-unset}" >> "$TEST_LAUNCHES"\n',True)
        write(self.loader,'#!/bin/sh\ncase "${TEST_LOADER_FAIL:-0}" in 1) exit 127;;2) echo \'libhv.so => not found\';exit 0;;esac\necho loader-ok\n',True)
        (self.proc/'exe').symlink_to(self.camera);(self.proc/'maps').write_text('temporary tested R2 library may already be mapped\n')
        (self.proc/'status').write_text('State:\tS (sleeping)\nVmSize:\t179624 kB\nVmRSS:\t15000 kB\nThreads:\t13\n')
        for n in ('libcc2-camera-reap-r2.so','libcc2-camera-recovery-r2.so'):shutil.copyfile(noop,self.source/n)
        helper='#!/bin/sh\n[ "${TEST_HELPER_FAIL:-0}" = 0 ] || exit 1\necho fixture-runtime-pass\n'
        write(self.source/'cc2-camera-reap-check',helper,True)
        substitutions={
            '01924b3dc427972838e995ac6763b566b85754a162d60fd6728553a405cd00cc':sha(self.init),
            'ec5633b17de329d5b18c3f8d8de6ebf389391a04193536d976ba167022d07b99':sha(self.camera),
            '8ef3a5c215462e3c9f32c3d4b85ea23e27c41e330d53b22d7cdeca9d100d016a':sha(noop),
            '08f2455a99fa5294153ae64ce34117c02f4f8d2dff8527dfd8386aae361a289a':sha(noop),
            'dce1180e3c04512fea1c4f9868b131f3fbe9bda187ebc9342f9db633f0a57ea0':hashlib.sha256(helper.encode()).hexdigest()}
        if native_profile:
            del substitutions['01924b3dc427972838e995ac6763b566b85754a162d60fd6728553a405cd00cc']
            substitutions['bcabb1fa405b066b06c229c6d8cc6e474a0ccc57e31a1b5b387d3b55945be563']=sha(self.init)
        for name in NAMES:
            text=(HERE/name).read_text()
            for old,new in substitutions.items():text=text.replace(old,new)
            text=re.sub(r'/(?:opt|etc|lib|proc|tmp)/',lambda m:str(self.root)+m[0],text)
            write(self.source/name,text,True)
        self.manifest()
        write(self.stub/'id','#!/bin/sh\necho 0\n',True)
        write(self.stub/'pidof','#!/bin/sh\n[ "${TEST_NO_CAMERA:-0}" = 0 ] || exit 1\necho 12345\n',True)
        write(self.stub/'wget','#!/bin/sh\nprintf \'%s\' "$TEST_STATE"\n',True)
        write(self.stub/'sync','#!/bin/sh\nexit 0\n',True)
        self.launches=self.root/'launches.txt'
        self.env=dict(os.environ,PATH=str(self.stub)+':'+os.environ['PATH'],TEST_STATE=IDLE,TEST_LAUNCHES=str(self.launches))
        for name in ['LD_PRELOAD','LD_LIBRARY_PATH','CC2_CAMERA_REAP_TRIAL','CC2_CAMERA_REAP_ENABLE_LAN','CC2_CAMERA_RECOVERY_TRIAL']:self.env.pop(name,None)
    def manifest(self):
        names=(*NAMES,'libcc2-camera-reap-r2.so','libcc2-camera-recovery-r2.so','cc2-camera-reap-check')
        (self.source/'SHA256SUMS').write_text(''.join(sha(self.source/n)+'  '+n+'\n' for n in names))
    def run(self,action,ok=True,**env):
        global CASES;CASES+=1
        r=subprocess.run([*SHELL,str(self.source/'manage.sh'),action],env=dict(self.env,**env),capture_output=True,text=True,timeout=15)
        assert (r.returncode==0)==ok,(action,r.stdout,r.stderr)
        return r
    def boot(self,ok=True,source=False,**env):
        global CASES;CASES+=1
        script=(self.source if source else self.base)/'boot-camera.sh'
        boot_env=dict(self.env,TEST_NO_CAMERA='1');boot_env.update(env)
        r=subprocess.run([*SHELL,str(script)],env=boot_env,capture_output=True,text=True,timeout=15)
        assert (r.returncode==0)==ok,(r.stdout,r.stderr)
        if not ok:return None
        return self.launches.read_text().splitlines()[-1].split('|')
    def unchanged(self):
        assert self.init.read_bytes()==self.original and not self.launches.exists()
for name in NAMES:
    subprocess.run([*SHELL,'-n',str(HERE/name)],check=True)
    text=(HERE/name).read_text()
    assert 'kill -' not in text and 'killall ' not in text and not any(line.strip()=='reboot' for line in text.splitlines())
with tempfile.TemporaryDirectory() as directory:
    parent=Path(directory);noop=parent/'noop.so'
    subprocess.run(['gcc','-shared','-fPIC','-x','c','-','-o',str(noop)],input='int fixture_noop;\n',text=True,check=True)
    f=Fixture(parent,noop);f.run('check');f.unchanged();f.run('install');assert not f.launches.exists()
    assert (f.base/'printer.original').read_bytes()==f.original and stat.S_IMODE(f.init.stat().st_mode)==0o755
    patched=f.init.read_text();assert patched.count('# CC2_CAMERA_RECOVERY_R2')==1 and '# CC2_CAMERA_BOOT_R1' not in patched
    assert patched.index('unchanged-gui')<patched.index('# CC2_CAMERA_RECOVERY_R2')<patched.index('unchanged-mqtt')
    expected=['1','1','1',str(f.base/'libcc2-camera-reap-r2.so')+':'+str(f.base/'libcc2-camera-recovery-r2.so')]
    assert f.boot()==expected and f.boot()==expected, 'must stay enabled across boots'
    f.run('verify');before=f.init.read_bytes();f.run('install');assert f.init.read_bytes()==before
    f.run('disable');assert f.boot()==['0','0','0','unset']
    f.run('enable');assert f.boot()==expected
    f.run('rollback');assert f.init.read_bytes()==f.original and not (f.base/'enabled').exists()
    f.run('rollback');assert f.init.read_bytes()==f.original
    # A retained canonical component after rollback or firmware reinstallation
    # must be recognized as dormant, without weakening unknown-startup refusal.
    before=f.init.read_bytes();f.run('check');assert f.init.read_bytes()==before
    f.run('install');assert f.boot()==expected
    retained=list(f.base.parents[1].glob('cc2-camera-before-reinstall-*'))
    assert len(retained)==1 and (retained[0]/'printer.original').read_bytes()==f.original
    f=Fixture(parent,noop);f.run('install');f.run('rollback')
    (f.base/'common.sh').write_text('untrusted retained script\n')
    names=(*NAMES,'libcc2-camera-reap-r2.so','libcc2-camera-recovery-r2.so','cc2-camera-reap-check')
    (f.base/'SHA256SUMS').write_text(''.join(sha(f.base/n)+'  '+n+'\n' for n in names))
    f.run('check',ok=False);f.run('install',ok=False);assert f.init.read_bytes()==f.original
    assert (f.base/'common.sh').read_text()=='untrusted retained script\n'
    f=Fixture(parent,noop);f.run('install');f.run('rollback')
    f.run('install',ok=False,TEST_STATE=IDLE.replace('age":2','age":99'))
    assert f.base.exists() and f.init.read_bytes()==f.original
    assert not list(f.base.parents[1].glob('cc2-camera-before-reinstall-*'))
    # Disabled payload, corruption, changed firmware/startup and loader failures
    # must launch only the native camera, rather than break printer startup.
    for mode in ['disabled','bad-enabled','library','camera','startup','loader','missing-dependency','helper','manifest','missing-common','symlink-enabled']:
        f=Fixture(parent,noop);f.run('install')
        env={}
        if mode=='disabled':(f.base/'enabled').unlink()
        elif mode=='bad-enabled':(f.base/'enabled').write_text('wrong')
        elif mode=='library':(f.base/'libcc2-camera-recovery-r2.so').write_text('bad')
        elif mode=='camera':f.camera.write_text(f.camera.read_text()+'# newer firmware\n')
        elif mode=='startup':f.init.write_text(f.init.read_text()+'# later change\n')
        elif mode=='loader':env['TEST_LOADER_FAIL']='1'
        elif mode=='missing-dependency':env['TEST_LOADER_FAIL']='2'
        elif mode=='helper':env['TEST_HELPER_FAIL']='1'
        elif mode=='manifest':(f.base/'SHA256SUMS').write_text('invalid')
        elif mode=='missing-common':(f.base/'common.sh').unlink()
        elif mode=='symlink-enabled':(f.base/'enabled').unlink();(f.base/'enabled').symlink_to(f.root/'missing')
        assert f.boot(**env)==['0','0','0','unset'],mode
    f=Fixture(parent,noop);f.run('install');f.boot(ok=False,TEST_NO_CAMERA='0');assert not f.launches.exists()
    # Preserve a later unrelated startup modification during rollback.
    f=Fixture(parent,noop);f.run('install');f.init.write_text(f.init.read_text()+'# unrelated\n');changed=f.init.read_bytes()
    f.run('rollback',ok=False);assert f.init.read_bytes()==changed
    f=Fixture(parent,noop);f.run('install');(f.base/'printer.original').write_text('bad backup');before=f.init.read_bytes()
    f.run('rollback',ok=False);assert f.init.read_bytes()==before
    # Guards must refuse before changing startup or launching anything.
    for env in [dict(TEST_STATE=IDLE.replace('true','false')),dict(TEST_STATE=IDLE.replace('"status":1','"status":2')),dict(TEST_STATE=IDLE.replace('age":2','age":99')),dict(TEST_HELPER_FAIL='1'),dict(TEST_LOADER_FAIL='1'),dict(TEST_NO_CAMERA='1')]:
        f=Fixture(parent,noop);f.run('install',ok=False,**env);f.unchanged();assert not f.base.exists()
    f=Fixture(parent,noop);write(f.old/'armed','ONE_BOOT_ONLY\n');f.run('install',ok=False);f.unchanged()
    f=Fixture(parent,noop);f.init.write_text(f.init.read_text()+'# changed\n');changed=f.init.read_bytes();f.run('install',ok=False);assert f.init.read_bytes()==changed
    f=Fixture(parent,noop);(f.source/'libcc2-camera-recovery-r2.so').write_text('tampered');f.run('install',ok=False);f.unchanged()
    f=Fixture(parent,noop);f.run('install');shutil.move(f.base,f.root/'payload-away')
    assert f.boot(source=True)==['0','0','0','unset']
    # A previous rollback retains the personal directory. Known original
    # startup must still be eligible for the normal application component.
    f=Fixture(parent,noop);personal=f.root/'opt/usr/cc2-camera-recovery-r2';personal.mkdir()
    f.run('check');f.run('install');assert f.base.exists() and not list(personal.iterdir())
    if os.environ.get('CC2_TEST_NATIVE_PROFILE')!='1':
        for failure in ('none','source','startup','helper'):
            f=Fixture(parent,noop);personal=f.root/'opt/usr/cc2-camera-recovery-r2';personal.mkdir()
            pins={
                'common.sh':'7aa2ffafba8d49bf657a4b414973410a6eea42e34061c9aefb9df9be30f4fc78',
                'manage.sh':'f4a6c1cfe6dc1b6ab2e936ad1787fba7ea7b71064ce551ddad00e00e0569c4ff',
                'boot-camera.sh':'57b9d994c3b1910bf6409580d588381afbea1d430ec335d7fd7bf86dc23e62ca',
                'SHA256SUMS':'8e983273fc42c0d85adea7719d051a7045cf8a8b0b842766a093ebb5c5791877'}
            for name in pins:write(personal/name,'fixture personal '+name+'\n')
            for name in ('libcc2-camera-reap-r2.so','libcc2-camera-recovery-r2.so','cc2-camera-reap-check'):
                shutil.copyfile(f.source/name,personal/name)
            (personal/'cc2-camera-reap-check').chmod(0o700)
            (personal/'printer.original').write_bytes(f.original)
            write(personal/'enabled','CAMERA_RECOVERY_R2_ENABLED\n')
            common=f.source/'common.sh';text=common.read_text()
            for name,pin in pins.items():text=text.replace(pin,sha(personal/name))
            common.write_text(text);f.manifest()
            old_line=f'( if [ -x {f.old}/boot-camera.sh ]; then exec {f.old}/boot-camera.sh; else exec {f.camera}; fi ) & # CC2_CAMERA_BOOT_R1'
            new_line=f'( if [ -x {personal}/boot-camera.sh ]; then exec {personal}/boot-camera.sh; else exec {f.camera}; fi ) & # CC2_CAMERA_RECOVERY_R2'
            f.init.write_text(f.init.read_text().replace(old_line,new_line))
            if failure=='source':write(personal/'common.sh','changed source\n')
            if failure=='startup':f.init.write_text(f.init.read_text()+'# unrelated\n')
            before=f.init.read_bytes();enabled=(personal/'enabled').read_bytes()
            env={'TEST_HELPER_FAIL':'1'} if failure=='helper' else {}
            f.run('check',ok=failure=='none',**env)
            if failure=='none':f.run('install')
            assert f.init.read_bytes()==before and not f.base.exists()
            assert (personal/'enabled').read_bytes()==enabled and not f.launches.exists()
            if failure=='none':
                # Reproduce migration from the personal trial to the canonical
                # 1.2.19 component, leaving the old qualified trial untouched.
                # check and install must choose the same canonical component.
                f.init.write_bytes(f.original);f.run('install')
                before=f.init.read_bytes()
                personal_before={p.name:p.read_bytes() for p in personal.iterdir()}
                f.run('check');f.run('install');f.run('verify')
                assert f.init.read_bytes()==before
                assert {p.name:p.read_bytes() for p in personal.iterdir()}==personal_before
                f.run('disable');f.run('check');f.run('install')
                assert not (f.base/'enabled').exists(), 'preserve canonical activation choice'
                # A corrupt canonical component or changed startup must fail,
                # rather than fall back to the obsolete personal directory.
                f.init.write_bytes(before+b'# unrelated startup change\n')
                f.run('check',ok=False);f.run('install',ok=False)
                f.init.write_bytes(before)
                (f.base/'libcc2-camera-recovery-r2.so').write_bytes(b'corrupt')
                f.run('check',ok=False);f.run('install',ok=False)
print(f'PASS: {CASES} persistent install/boot/disable/enable/exact-rollback/fallback/refusal cases; shell={SHELL}; no printer addressed')
