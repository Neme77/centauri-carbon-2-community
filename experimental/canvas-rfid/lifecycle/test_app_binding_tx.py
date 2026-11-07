"""Actual binding transaction; process exits model interruptions, no device I/O."""
from pathlib import Path
import fcntl, hashlib, json, os, shutil, struct, subprocess, tempfile

HERE=Path(__file__).resolve().parent
NAMES=['coordinator','capture','app-integration.json','activate-on-ready']
WITH_TRANSFER=os.environ.get('CC2_TEST_BIND_TRANSFER')=='1'
PRESERVE_LIFECYCLE=os.environ.get('CC2_TEST_PRESERVE_LIFECYCLE')=='1'
if WITH_TRANSFER:NAMES.append('transfer-worker')
MODES={name:0o700 if name in ('coordinator','capture','transfer-worker') else 0o600 for name in NAMES}
OLD={n:('original '+n+'\n').encode() for n in NAMES}
NEW={n:('updated '+n+'\n').encode() for n in NAMES}
OLD_APP=b'qualified old application fixture';NEW_APP=b'qualified new application fixture'
sha=lambda b:hashlib.sha256(b).hexdigest()
PROFILE_BYTES=b'qualified recovery profile fixture'
PROFILE=sha(PROFILE_BYTES) if PRESERVE_LIFECYCLE else 'a7'*32
BASE_CASES=['normal','retry','old-app','new-source','old-target','source-symlink','target-symlink',
    'target-hardlink','barrier-absent','barrier-public','wrong-barrier-fd','component-public',
    'component-symlink','tx-public','tx-symlink','nonactive','bad-journal','wrong-profile',
    'foreign-state','bad-state','bad-snapshot','snapshot-symlink','changed-generation',
    'foreign-target-after-prepare','wrong-new-app','lifecycle-lock','binding-lock']
CRASH_CASES=['snapshot:'+str(i) for i in range(len(NAMES))]+['state:1']
APPLY_CRASH=['replace:'+str(i) for i in range(len(NAMES))]+['state:2','state:3']
ROLLBACK_CRASH=['replace:'+str(i) for i in range(len(NAMES))]+['state:4','state:5']

def put(p,data,mode=0o600):
    p.parent.mkdir(parents=True,exist_ok=True,mode=0o700);p.write_bytes(data);p.chmod(mode)

def journal(generation=5,phase=5,profile=PROFILE):
    b=bytearray(128);b[:14]=b'CC2-CANVAS-LC1';b[16]=phase
    struct.pack_into('<Q',b,24,generation);b[32:48]=bytes(range(1,17))
    b[48:84]=b'12345678-1234-1234-1234-123456789abc';b[84:116]=bytes.fromhex(profile)
    return bytes(b)+sha(b).encode()

def run():
    count=0
    with tempfile.TemporaryDirectory(prefix='cc2-app-binding-') as temporary:
        root=Path(temporary);fixture=root/'fixture';component=fixture/'component';source=fixture/'source'
        tx=fixture/'transaction';barrier=fixture/'barrier';app=fixture/'cc2-control';other=fixture/'other'
        config=root/'config.h'
        rows=['{%s,%s,%s,0%o}'%(json.dumps(n),json.dumps(sha(OLD[n])),json.dumps(sha(NEW[n])),MODES[n]) for n in NAMES]
        config.write_text('static const struct au_plan test_plan={%s,%s,%s,{%s}};\n'%(
            json.dumps(sha(OLD_APP)),json.dumps(sha(NEW_APP)),json.dumps(PROFILE),','.join(rows)))
        for arch,compiler,extra,runner in [
            ('host','gcc',[],[]),
            ('arm','arm-linux-gnueabihf-gcc',['-static','-D_TIME_BITS=32','-D_FILE_OFFSET_BITS=32'],['qemu-arm']),
            ('sanitizers','gcc',['-fsanitize=address,undefined','-fno-sanitize-recover=all','-fno-pie','-no-pie'],[])]:
            binary=root/(arch+'-test')
            defines={'AU_COMPONENT':str(component),'AU_DIRECTORY':str(tx),'AU_APP':str(app),
                'AU_BARRIER':str(barrier),'AU_TEST_CONFIG':str(config),'AU_TEST_SOURCE':str(source),'AU_TEST_OTHER':str(other)}
            subprocess.run([compiler,'-O2','-std=c11','-Wall','-Wextra','-Wpedantic','-Werror',*extra,
                *(['-DAU_BIND_TRANSFER_WORKER=1'] if WITH_TRANSFER else []),
                *(['-DAU_PRESERVE_LIFECYCLE=1'] if PRESERVE_LIFECYCLE else []),
                *['-D'+k+'='+json.dumps(v) for k,v in defines.items()],HERE/'test_app_binding_tx.c','-o',binary],check=True,timeout=120)
            cases=BASE_CASES+[('prepare-crash',x) for x in CRASH_CASES]+[('apply-crash',x) for x in APPLY_CRASH]+[('rollback-crash',x) for x in ROLLBACK_CRASH]
            if PRESERVE_LIFECYCLE:
                cases=[case for case in cases if case!='nonactive']
                cases += [('lifecycle-phase',phase) for phase in range(11)]
                cases.append('no-journal')
            for case in cases:
                if fixture.exists():shutil.rmtree(fixture)
                fixture.mkdir(mode=0o700);component.mkdir(mode=0o700);source.mkdir(mode=0o700);barrier.mkdir(mode=0o700);other.mkdir(mode=0o700)
                for n in NAMES:put(component/n,OLD[n],MODES[n]);put(source/n,NEW[n],MODES[n])
                put(component/'lifecycle',journal());put(component/'original-stock-44.bin',b'untouched stock fixture');put(component/'startup-sentinel',b'untouched startup fixture')
                put(app,OLD_APP,0o755)
                env=dict(os.environ)
                def invoke(action,crash=None):
                    e=dict(env)
                    if crash:e['AU_TEST_CRASH']=crash
                    return subprocess.run([*runner,binary,action],env=e,capture_output=True,text=True,timeout=12)
                original_journal=(component/'lifecycle').read_bytes();lock=None
                kind=case[0] if isinstance(case,tuple) else case
                if kind=='old-app':put(app,b'foreign app',0o755)
                if kind=='new-source':put(source/NAMES[3],b'foreign')
                if kind=='old-target':put(component/NAMES[3],b'foreign')
                if kind in ['source-symlink','target-symlink','target-hardlink']:
                    path=(source if kind=='source-symlink' else component)/NAMES[0];saved=fixture/'foreign';path.rename(saved)
                    if kind=='target-hardlink':os.link(saved,path)
                    else:path.symlink_to(saved)
                if kind=='barrier-absent':barrier.rmdir()
                if kind=='barrier-public':barrier.chmod(0o755)
                if kind=='wrong-barrier-fd':env['AU_TEST_WRONG_FD']='1'
                if kind=='component-public':component.chmod(0o755)
                if kind=='component-symlink':component.rename(fixture/'saved-component');component.symlink_to(fixture/'saved-component',target_is_directory=True)
                if kind in ['tx-public','tx-symlink','foreign-state','binding-lock']:
                    tx.mkdir(mode=0o700)
                    if kind=='tx-public':tx.chmod(0o755)
                    if kind=='tx-symlink':tx.rename(fixture/'saved-tx');tx.symlink_to(fixture/'saved-tx',target_is_directory=True)
                    if kind=='foreign-state':put(tx/'state',b'foreign')
                if kind=='nonactive':put(component/'lifecycle',journal(phase=3))
                if kind=='lifecycle-phase':put(component/'lifecycle',journal(phase=case[1]))
                if kind=='no-journal':
                    put(component/'recovery-profile',PROFILE_BYTES)
                    (component/'lifecycle').unlink()
                else:original_journal=(component/'lifecycle').read_bytes()
                if kind=='bad-journal':put(component/'lifecycle',journal()[:-1]+b'x')
                if kind=='wrong-profile':put(component/'lifecycle',journal(profile='b7'*32))
                if kind in ['lifecycle-lock','binding-lock']:
                    p=(component/'lifecycle.lock') if kind=='lifecycle-lock' else tx/'binding.lock'
                    put(p,b'');lock=p.open('r+b');fcntl.flock(lock,fcntl.LOCK_EX|fcntl.LOCK_NB)
                before={n:(component/n).read_bytes() for n in NAMES}
                prepared=invoke('--prepare',case[1] if kind=='prepare-crash' else None)
                early=kind in ['old-app','new-source','old-target','source-symlink','target-symlink','target-hardlink','barrier-absent','barrier-public','wrong-barrier-fd','component-public','component-symlink','tx-public','tx-symlink','nonactive','bad-journal','wrong-profile','foreign-state','lifecycle-lock','binding-lock']
                try:
                    if early:
                        assert prepared.returncode!=0,(arch,case,'unexpected prepare',prepared.stderr)
                        assert before=={n:(component/n).read_bytes() for n in NAMES}
                    else:
                        if kind=='prepare-crash':
                            assert prepared.returncode==91,(arch,case,prepared.stderr)
                            prepared=invoke('--prepare')
                        assert prepared.returncode==0,(arch,case,prepared.stderr)
                        assert invoke('--prepare').returncode==0,(arch,case,'not idempotent')
                        if kind=='bad-state':put(tx/'state',b'foreign')
                        if kind=='bad-snapshot':put(tx/'new.3',b'foreign')
                        if kind=='snapshot-symlink':(tx/'new.3').rename(tx/'saved');(tx/'new.3').symlink_to(tx/'saved')
                        if kind=='changed-generation':put(component/'lifecycle',journal(generation=6))
                        if kind=='foreign-target-after-prepare':put(component/NAMES[3],b'foreign')
                        put(app,b'foreign application' if kind=='wrong-new-app' else NEW_APP,0o755)
                        expected=kind not in ['bad-state','bad-snapshot','snapshot-symlink','changed-generation','foreign-target-after-prepare','wrong-new-app']
                        immediately_before={n:(component/n).read_bytes() for n in NAMES}
                        applied=invoke('--apply',case[1] if kind=='apply-crash' else None)
                        if kind=='apply-crash':
                            assert applied.returncode==91,(arch,case,applied.stderr)
                            applied=invoke('--apply')
                        assert (applied.returncode==0)==expected,(arch,case,applied.stderr)
                        if expected:
                            assert {n:(component/n).read_bytes() for n in NAMES}==NEW
                            assert invoke('--apply').returncode==0
                            rolled=invoke('--rollback',case[1] if kind=='rollback-crash' else None)
                            if kind=='rollback-crash':
                                assert rolled.returncode==91,(arch,case,rolled.stderr)
                                rolled=invoke('--rollback')
                            assert rolled.returncode==0,(arch,case,rolled.stderr)
                            assert {n:(component/n).read_bytes() for n in NAMES}==OLD
                            assert invoke('--rollback').returncode==0
                            assert invoke('--apply').returncode!=0,'rollback intent must not roll forward'
                        else:assert {n:(component/n).read_bytes() for n in NAMES}==immediately_before
                    if kind=='no-journal':
                        assert not (component/'lifecycle').exists()
                    elif kind not in ['nonactive','bad-journal','wrong-profile','changed-generation']:
                        assert (component/'lifecycle').read_bytes()==original_journal,(arch,case,'native journal modified')
                    assert (component/'original-stock-44.bin').read_bytes()==b'untouched stock fixture'
                    assert (component/'startup-sentinel').read_bytes()==b'untouched startup fixture'
                finally:
                    if lock:lock.close()
                count+=1
    print(f'PASS: {count} actual app-binding transaction host/ARM/sanitizer cases; interrupted file/state commits resume, rollback exact, native journal/startup/stock retained',flush=True)
    return count
if __name__=='__main__':run()
