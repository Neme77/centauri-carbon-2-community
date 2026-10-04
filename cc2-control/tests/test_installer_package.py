"""Exercise packaging and updater rollback without SSH or a real printer."""
import hashlib, importlib.util, io, os, pathlib, subprocess, tarfile, tempfile, zipfile
ROOT=pathlib.Path(__file__).resolve().parents[1]
spec=importlib.util.spec_from_file_location('updater',ROOT/'installer/package.py')
pkg=importlib.util.module_from_spec(spec);spec.loader.exec_module(pkg)
with tempfile.TemporaryDirectory() as tmp:
    base=pathlib.Path(tmp);prepared=base/'prepared';(prepared/'web/locales').mkdir(parents=True)
    binary=bytearray(64);binary[:6]=b'\x7fELF\x01\x01';binary[18]=40
    (prepared/'cc2-control').write_bytes(binary)
    (prepared/'web/index.html').write_text('UI');(prepared/'web/locales/en.json').write_text('{}')
    source=base/'source.zip';source.write_bytes(b'test source');output=base/'updater.zip'
    pkg.package(prepared,ROOT/'scripts',source,output)
    with zipfile.ZipFile(output) as z:
        files={pathlib.Path(n).name:z.read(n) for n in z.namelist()}
        for line in files['SHA256SUMS'].decode().splitlines():
            digest,name=line.split('  ',1);assert hashlib.sha256(files[name]).hexdigest()==digest
    for scenario in ('success','delayed','busy','rollback'):
        fixture=base/scenario;fixture.mkdir();stage=fixture/'stage';stage.mkdir()
        with tarfile.open(fileobj=io.BytesIO(files['cc2-control-1.1.31-payload.tar.gz']),mode='r:gz') as t:
            for m in t.getmembers():
                p=stage/m.name;p.parent.mkdir(parents=True,exist_ok=True);p.write_bytes(t.extractfile(m).read());p.chmod(m.mode)
                assert m.mode==(0o755 if m.name=='cc2-control' or m.name.endswith(('.sh','.init')) else 0o644)
        for line in (stage/'SHA256SUMS').read_text().splitlines():
            digest,name=line.split('  ',1);assert hashlib.sha256((stage/name).read_bytes()).hexdigest()==digest
        target=fixture/'opt/usr/cc2-control';target.mkdir(parents=True);(target/'cc2-control').write_bytes(b'OLD')
        keep=('cc2-control.conf','material-presets.json','ui-preferences.json')
        for name in keep:(target/name).write_text('KEEP '+name)
        init=fixture/'init';marker=fixture/'running'
        init.write_text('#!/bin/sh\ncase "$1" in start) touch "'+str(marker)+'";; stop) rm -f "'+str(marker)+'";; esac\n');init.chmod(0o755)
        (stage/'cc2-control.init').write_bytes(init.read_bytes())
        commands=fixture/'bin';commands.mkdir()
        state=2 if scenario=='busy' else 1
        (commands/'wget').write_text('#!/bin/sh\ncase "$*" in *api/printer*) echo \'{"connected":true,"machine":{"status":'+str(state)+',"x":0},"last_message_age":0,"x":0}\';; *) echo \'{"service":"cc2-control","version":"1.1.31","mqtt_registered":true,"snapshot_received":true}\';; esac\n')
        if scenario == 'delayed':
            # Simulate health becoming available after the 60-second launcher
            # delay. The old 30-attempt limit must fail this scenario.
            counter = fixture / 'health-attempts'
            wget = commands / 'wget'
            text = wget.read_text()
            text = text.replace('case "$*" in',
                'if [ -f "' + str(marker) + '" ]; then\n'
                '  case "$*" in *api/health*)\n'
                '    n=$(cat "' + str(counter) + '" 2>/dev/null || echo 0)\n'
                '    n=$((n+1)); echo "$n" > "' + str(counter) + '"\n'
                '    [ "$n" -gt 35 ] || exit 1;; esac\nfi\ncase "$*" in')
            wget.write_text(text)
        (commands/'pidof').write_text('#!/bin/sh\nif [ -f "'+str(marker)+'" ]; then echo 999; else exit 1; fi\n')
        (commands/'sleep').write_text('#!/bin/sh\nexit 0\n')
        for p in commands.iterdir():p.chmod(0o755)
        (stage/'SHA256SUMS').write_text(''.join(hashlib.sha256(p.read_bytes()).hexdigest()+'  '+p.relative_to(stage).as_posix()+'\n' for p in sorted(stage.rglob('*')) if p.is_file() and p.name!='SHA256SUMS'))
        script=(stage/'install-on-printer.sh').read_text().replace('/opt/usr/cc2-control',str(target)).replace('/etc/init.d/cc2-control',str(init)).replace('/tmp/cc2-control-install.lock',str(fixture/'lock')).replace('/proc/$RUN_PID/exe',str(target/'cc2-control'))
        script=script.replace('STAGE=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)','STAGE="'+str(stage)+'"')
        if scenario=='rollback':script=script.replace(hashlib.sha256(binary).hexdigest(),'0'*64)
        path=fixture/'run.sh';path.write_text(script)
        result=subprocess.run(['sh',str(path)],env=dict(os.environ,PATH=str(commands)+':'+os.environ['PATH']),capture_output=True,text=True,timeout=10)
        assert (result.returncode==0)==(scenario in ('success','delayed')),result.stdout+result.stderr
        assert (target/'cc2-control').read_bytes()==(bytes(binary) if scenario in ('success','delayed') else b'OLD')
        if scenario == 'delayed': assert int(counter.read_text()) == 36
        for name in keep:assert (target/name).read_text()=='KEEP '+name
        assert not (fixture/'lock').exists()
        if scenario == 'success':
            # Firmware installations keep their binary in /opt/inst; their
            # /opt/usr backup contains configuration only.
            backup = next(target.parent.glob('cc2-control-backup-*'))
            (backup/'installation/cc2-control').unlink()
            restore = (backup/'restore.sh').read_text().replace('/opt/usr/cc2-control',str(target)).replace('/etc/init.d/cc2-control',str(init)).replace('/tmp/cc2-control-install.lock',str(fixture/'lock'))
            (backup/'restore.sh').write_text(restore)
            result = subprocess.run(['sh',str(backup/'restore.sh')],env=dict(os.environ,PATH=str(commands)+':'+os.environ['PATH']),capture_output=True,text=True,timeout=10)
            assert result.returncode == 0, result.stdout + result.stderr
            assert not (target/'cc2-control').exists()
            for name in keep: assert (target/name).read_text() == 'KEEP '+name
            assert marker.exists() and not (fixture/'lock').exists()
    (prepared/'cc2-control').write_bytes(b'wrong architecture')
    try:pkg.package(prepared,ROOT/'scripts',source,output)
    except ValueError:pass
    else:raise AssertionError('Non-ARM binary accepted')
print('PASS: package checksums/modes, ARM guard, Idle rejection, config preservation, install/rollback simulation and configuration-only firmware backup restore')
