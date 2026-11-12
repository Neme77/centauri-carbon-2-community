"""Offline qualification; data files only, no printer/SSH/vendor execution."""
from pathlib import Path
from contextlib import ExitStack
import ast
import fcntl
import hashlib
import importlib.util
import json
import os
import shutil
import struct
import subprocess
import sys
import tarfile
import tempfile

HERE = Path(__file__).resolve().parent
PROJECT = HERE.parents[1]
SOURCE = PROJECT / 'work/cc2-13-port/source'
LIFECYCLE = SOURCE / 'experimental/canvas-rfid/lifecycle'
PROGRAM = SOURCE / 'experimental/firmware-port/upgrade/upgrade_evidence.c'
sys.path.insert(0, str(LIFECYCLE))
from test_stage_worker import fixture, put, seed_journal, PATHS
sha = lambda data: hashlib.sha256(data).hexdigest()
assert os.geteuid() == 0, 'Run this qualification as WSL root'
stock_path = PROJECT / 'work/cc2-stock-02.01.01.13/selected/opt/inst/ota/canvas_gd32-20251029_184624-00.00.00.44-app.bin'
stock = stock_path.read_bytes()
assert sha(stock) == 'f1267ee3219dcad73efffa8dc38e30ff0f17c02c0a1674cbead48a99bdff3228'
arm = '/opt/cc2-cross/toolchain-out/bin/arm-cortex_a15-linux-gnueabihf-gcc'
qemu = shutil.which('qemu-arm') or shutil.which('qemu-arm-static')
assert Path(arm).is_file() and qemu and shutil.which('gcc')
flags = ['-O2', '-std=c11', '-Wall', '-Wextra', '-Wpedantic', '-Werror']
architectures = [('host', 'gcc', [], []), ('arm', arm, ['-static'], [qemu]),
                 ('sanitizers', 'gcc', ['-fsanitize=address,undefined', '-fno-sanitize-recover=all', '-fno-pie', '-no-pie'], [])]
cases = ('absent', 'dormant', 'restored-125', 'restored-126', 'restored-127', 'restored-128',
         'immutable-restored', 'active', 'pending', 'activation-request', 'orphan-marker',
         'journal-bad', 'profile-bad', 'helper-bad', 'backup-bad', 'target-changed',
         'snapshot-bad', 'receipt-bad', 'locked', 'journal-link', 'journal-hardlink',
         'unknown-protocol', 'unknown-profile', 'immutable-wrong-role', 'metadata-bad',
         'profile-dot-path', 'profile-duplicate-role')
count = 0


def receipts(component, tree, old, new, translate=True):
    modes = [0o755, 0o700, 0o600, 0o700, 0o700, 0o700, 0o644, 0o600]
    rows = []
    for i, path in enumerate(PATHS):
        put(tree / path[1:], old[i], modes[i])
        put(component / f'startup-tx/{i}.old', old[i])
        put(component / f'startup-tx/{i}.new', new[i])
        target = str(tree / path[1:]) if translate else path
        rows.append(f'{i} {modes[i]:04o} {modes[i]:04o} {sha(old[i])} {sha(new[i])} {target}\n')
    digest = sha(''.join(rows).encode()).encode() + b'\n'
    put(component / 'startup-tx/ready', digest)
    put(component / 'startup-tx/restored', digest)


def captured_restore(tree, archive_path):
    """Retained real component; remap only live target paths/receipt locations."""
    component=tree/'opt/usr/cc2-rfid'
    component.mkdir(parents=True,mode=0o700)
    total=0
    with tarfile.open(archive_path) as archive:
        for member in archive:
            name=Path(member.name)
            if not name.parts or name.parts[0]!='cc2-rfid':continue
            relative=Path(*name.parts[1:])
            assert not relative.is_absolute() and '..' not in relative.parts
            destination=component/relative
            if member.isdir():destination.mkdir(parents=True,exist_ok=True,mode=0o700)
            else:
                assert member.isfile() and not member.issym() and not member.islnk()
                total+=member.size
                assert total<=8*1024*1024
                put(destination,archive.extractfile(member).read(),member.mode)
    plan=(component/'startup-plan').read_text().splitlines()[3:]
    assert len(plan)==8
    original=[];translated=[]
    for i,(path,row) in enumerate(zip(PATHS,plan)):
        index,old_mode,new_mode,old_sha,new_sha=row.split()
        assert int(index)==i
        old=(component/f'startup-tx/{i}.old').read_bytes()
        new=(component/f'payload/{i}').read_bytes()
        assert sha(old)==old_sha and sha(new)==new_sha
        put(tree/path[1:],old,int(old_mode,8))
        prefix=f'{i} {old_mode} {new_mode} {old_sha} {new_sha} '
        original.append(prefix+path+'\n')
        translated.append(prefix+str(tree/path[1:])+'\n')
    expected=sha(''.join(original).encode()).encode()+b'\n'
    assert (component/'startup-tx/ready').read_bytes()==expected
    assert (component/'startup-tx/restored').read_bytes()==expected
    # The production receipt includes absolute target paths. Preserve and check
    # the original first; adapt that location-only binding for the fake tree.
    moved=sha(''.join(translated).encode()).encode()+b'\n'
    put(component/'startup-tx/ready',moved)
    put(component/'startup-tx/restored',moved)
    return component


with tempfile.TemporaryDirectory(prefix='cc2-upgrade-evidence-') as temporary:
    root = Path(temporary)
    tree = root / 'tree'
    for arch, compiler, extra, runner in architectures:
        binary = root / (arch + '-evidence')
        subprocess.run([compiler, *flags, *extra, '-DPREPARE_PREFIX=' + json.dumps(str(tree)),
                        str(PROGRAM), '-lm', '-o', str(binary)], check=True, timeout=120)
        for case in cases:
            if tree.exists(): shutil.rmtree(tree)
            original = root / 'component'
            if original.exists(): shutil.rmtree(original)
            original, _, _, _, old, new, _, _ = fixture(root, b'inert app data', stock)
            component = tree / 'opt/usr/cc2-rfid'
            original.rename(component)
            version = case.removeprefix('restored-')
            put(component / 'coordinator', ('coordinator-' + version).encode(), 0o700)
            put(component / 'capture', ('capture-' + version).encode(), 0o700)
            put(component / 'lifecycle.lock', b'')
            put(component / 'original-stock-44.bin', stock)
            receipts(component, tree, old, new)
            profile = (component / 'recovery-profile').read_bytes()
            if case.startswith('immutable'):
                (component / 'startup-plan').unlink()
                pairs = [('bootstrap', tree/'etc/init.d/cc2-reactor-launch'),
                         ('native', tree/'opt/usr/cc2-reactor-runtime-v2/elegoo_printer'),
                         ('guard', component/'libcc2-rfid.so'),
                         ('entry', tree/'opt/inst/cc2-printer-extensions13/launch.sh'),
                         ('recover', tree/'opt/inst/cc2-printer-extensions13/recover-worker'),
                         ('marker', tree/'opt/inst/cc2-printer-extensions13/rfid-enabled.template')]
                profile = ''.join(f'{role} {sha(b"role")} {path}\n' for role, path in pairs).encode()
                if case == 'immutable-wrong-role': profile = profile.replace(b'/launch.sh', b'/foreign.sh')
                put(component/'recovery-profile', profile)
            if case == 'unknown-profile':
                profile = profile.replace(b'bootstrap ', b'foreign ')
                put(component/'recovery-profile', profile)
            elif case == 'profile-dot-path':
                profile = profile.replace(b'/startup-plan\n', b'/..\n')
                put(component/'recovery-profile', profile)
            elif case == 'profile-duplicate-role':
                profile = profile.replace(b'native ', b'bootstrap ')
                put(component/'recovery-profile', profile)
            metadata = dict(protocol=2 if case == 'unknown-protocol' else 1,
                            app_sha256=sha(('app-' + version).encode()),
                            coordinator_sha256=sha((component/'coordinator').read_bytes()),
                            capture_sha256=sha((component/'capture').read_bytes()), profile_sha256=sha(profile))
            put(component/'app-integration.json', (json.dumps(metadata, sort_keys=True)+'\n').encode())
            seed_journal(component, profile, 5 if case == 'active' else 10)
            put(component/'enabled', b'CC2-RFID-NATIVE-R2\n')
            lock = None
            if case == 'absent': shutil.rmtree(component)
            elif case in ('dormant', 'orphan-marker'):
                (component/'lifecycle').unlink()
                shutil.rmtree(component/'startup-tx')
                if case == 'dormant': (component/'enabled').unlink()
            elif case == 'pending': put(component/'.lifecycle-test.next', b'pending')
            elif case == 'activation-request': put(component/'activation-request', b'pending')
            elif case == 'journal-bad': put(component/'lifecycle', b'bad')
            elif case == 'profile-bad': put(component/'recovery-profile', b'bad')
            elif case == 'helper-bad': put(component/'coordinator', b'bad', 0o700)
            elif case == 'metadata-bad': put(component/'app-integration.json', b'{"protocol":1,')
            elif case == 'backup-bad': put(component/'original-stock-44.bin', b'bad')
            elif case == 'target-changed': put(tree/PATHS[0][1:], b'changed', 0o755)
            elif case == 'snapshot-bad': put(component/'startup-tx/0.old', b'changed')
            elif case == 'receipt-bad': put(component/'startup-tx/restored', b'changed')
            elif case == 'locked':
                lock=(component/'lifecycle.lock').open('rb'); fcntl.flock(lock, fcntl.LOCK_EX|fcntl.LOCK_NB)
            elif case == 'journal-link':
                (component/'lifecycle').rename(root/'journal-target')
                (component/'lifecycle').symlink_to(root/'journal-target')
            elif case == 'journal-hardlink': os.link(component/'lifecycle', root/'journal-hardlink')
            before = {str(p.relative_to(tree)):p.read_bytes() for p in tree.rglob('*') if p.is_file() and not p.is_symlink()}
            try:
                result=subprocess.run([*runner,str(binary),'--inspect'],capture_output=True,text=True,timeout=15)
                expected=case in ('absent','dormant','immutable-restored') or case.startswith('restored-')
                assert (result.returncode==0)==expected,(arch,case,result.stdout,result.stderr)
                if expected:assert result.stdout.strip()==('ABSENT' if case=='absent' else 'DORMANT' if case=='dormant' else 'RESTORED')
                assert before=={str(p.relative_to(tree)):p.read_bytes() for p in tree.rglob('*') if p.is_file() and not p.is_symlink()},(arch,case,'probe wrote files')
            finally:
                if lock:lock.close()
                for leaf in ('journal-target','journal-hardlink'):
                    if (root/leaf).exists(): (root/leaf).unlink()
            count+=1
            print(f'PASS {arch} version-independent Canvas evidence {case}',flush=True)
        captured=Path('/mnt/c/Users/User/Desktop/CC2-Upgrade-20261111-192944/community-stages.tar')
        if captured.is_file():
            shutil.rmtree(tree)
            captured_restore(tree,captured)
            result=subprocess.run([*runner,str(binary),'--inspect'],capture_output=True,text=True,timeout=20)
            assert result.returncode==0 and result.stdout.strip()=='RESTORED',(arch,'captured restored component',result.stderr)
            print(f'PASS {arch} captured real restored 1.2.27 component; original receipts verified before fixture remapping',flush=True)
            count+=1
    print(f'PASS: {count} actual evidence probe cases; no retained code executed and no evidence files changed',flush=True)
    production=root/'cc2-upgrade-evidence'
    subprocess.run([arm,*flags,'-static','-s','-mcpu=cortex-a7','-mfpu=neon-vfpv4','-mfloat-abi=hard',str(PROGRAM),'-lm','-o',str(production)],check=True,timeout=120)
    elf=production.read_bytes()
    assert elf[:6]==b'\x7fELF\x01\x01' and struct.unpack_from('<H',elf,18)[0]==40, 'Expected little-endian ELF32 ARM'
    headers=subprocess.check_output([arm.removesuffix('gcc')+'readelf','-l',str(production)],text=True,timeout=15)
    assert 'INTERP' not in headers, 'The preparation probe must be static'
    # Run all shell backup/quarantine cases under both host sh and stock ARM ash.
    spec=importlib.util.spec_from_file_location('prepare_tests',HERE/'Test-Prepare-Upgrade.py')
    preparation=importlib.util.module_from_spec(spec);spec.loader.exec_module(preparation)
    with ExitStack() as stack:
        preparation.check(['bash'])
        preparation.check(preparation.stock_busybox(stack)+['ash'])
    destination=HERE/'cc2-upgrade-evidence'
    shutil.copyfile(production,destination)
    (HERE/'cc2-upgrade-evidence.sha256').write_text(sha(destination.read_bytes())+'\n')
    receipt=dict(probe_sha256=sha(destination.read_bytes()),evidence_cases=count,
                 shell_cases=36,protocol=1,printer_accessed=False,firmware_changed=False,
                 source_sha256=sha(PROGRAM.read_bytes()))
    (HERE/'preparation-qualification.json').write_text(json.dumps(receipt,indent=2)+'\n')
    print('PASS UNIVERSAL PREPARATION: ARM evidence probe built only after all tests passed; no printer accessed',flush=True)
