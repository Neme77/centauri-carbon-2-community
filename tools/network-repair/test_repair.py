"""Actual repair script under BusyBox, using retained release input in temporary fixtures."""
from pathlib import Path
import argparse
import hashlib
import os
import shutil
import subprocess
import tempfile

REPO = Path(__file__).resolve().parents[2]
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--fixture', required=True, type=Path, help='Reviewed published cc2-connections component directory; no download')
INPUT = parser.parse_args().fixture.resolve()
SCRIPT = REPO / 'tools/network-repair/repair-dhcp.sh'
sha = lambda data: hashlib.sha256(data).hexdigest()
assert sha((INPUT/'SHA256SUMS').read_bytes()) == '7d166a1df548e0efbb0c7330facbb46d6ae7b9c31dd002913d0d3ee6284a22c3'
OLD = (INPUT/'native-udhcpc_wlan0').read_bytes()
NEW = (INPUT/'wifi-client.sh').read_bytes()
assert sha(OLD) == 'b76f68e345f1221d835d1a0d6279827c5df4823c2e7fadfe0298cd49ec7bb7df'
assert sha(NEW) == '8061ab23e446c3cee49b6148b01eee098e04cfb4e70c271306f16832dd4cb56f'
busybox = shutil.which('busybox')
assert busybox, 'BusyBox is required; no GNU-shell-only qualification'
mapping = {
    '/etc/hotplug.d/net/90-cc2-network-manager': 'hotplug.sh',
    '/etc/init.d/cc2-camera-launch': 'camera-launch.sh',
    '/etc/init.d/cc2-network-manager': 'init.sh',
    '/etc/init.d/cc2-reactor-launch': 'reactor-launch.sh',
    '/etc/wifi/udhcpc_wlan0': 'wifi-client.sh',
    '/opt/inst/cc2-printer-extensions13/launch.sh': 'printer-launch.sh',
    '/opt/usr/wifi/udhcpc_wlan0': 'wifi-client.sh',
    '/usr/share/udhcpc/set_lan.sh': 'native-lease.sh',
}
cases = ['healthy-check', 'healthy-apply', 'recognised-check', 'recognised-apply',
         'foreign-dhcp', 'changed-source', 'changed-manager', 'changed-startup',
         'changed-manifest', 'busy', 'pending', 'not-ready', 'symlink-dhcp',
         'symlink-parent', 'empty-rfid', 'postcheck-rollback']
for case in cases:
    with tempfile.TemporaryDirectory(prefix='cc2-dhcp-repair-test-') as temporary:
        root = Path(temporary)
        source = root/'opt/inst/cc2-connections'
        component = root/'opt/usr/cc2-network-manager'
        dhcp = root/'opt/usr/wifi/udhcpc_wlan0'
        rfid = root/'opt/usr/cc2-rfid'
        shutil.copytree(INPUT, source)
        component.mkdir(parents=True)
        shutil.copy2(source/'control', component/'control')
        for original, name in mapping.items():
            target = root/original.lstrip('/')
            target.parent.mkdir(parents=True, exist_ok=True)
            target.write_bytes((source/name).read_bytes())
            target.chmod(0o755)
        manifest = ''.join(f'{sha((source/name).read_bytes())}  {root/original.lstrip("/")}\n' for original,name in sorted(mapping.items()))
        (component/'startup.sha256').write_text(manifest)
        dhcp.write_bytes(NEW if case.startswith('healthy') else OLD)
        rfid.mkdir()
        (rfid/'lifecycle').write_bytes(b'RFID state must not change')
        (rfid/'coordinator').write_bytes(b'never execute this fixture')
        cfg = root/'opt/usr/cc2-network-state'
        cfg.mkdir()
        (cfg/'config.bin').write_bytes(b'Wi-Fi-only saved configuration; no USB adapter')
        calibration = root/'opt/usr/cfg/calibration.bin'
        calibration.parent.mkdir()
        calibration.write_bytes(b'calibration sentinel')
        bindir = root/'bin'
        bindir.mkdir()
        subprocess.run([busybox, '--install', '-s', str(bindir)], check=True)
        def stub(name, text):
            p=bindir/name
            p.unlink(missing_ok=True)
            p.write_text('#!/bin/sh\n'+text+'\n')
            p.chmod(0o755)
        state = '{"state_valid":true,"ready":true,"busy":false,"phase":"idle","pending":null}'
        if case=='busy': state=state.replace('"busy":false','"busy":true')
        if case=='pending': state=state.replace('"phase":"idle"','"phase":"pending"')
        if case=='not-ready': state=state.replace('"ready":true','"ready":false')
        stub('wget', "printf '%s\\n' '"+state+"'")
        stub('sync', 'exit 0')
        if case=='foreign-dhcp': dhcp.write_bytes(b'foreign DHCP code')
        if case=='changed-source': (source/'control').write_bytes(b'altered source manager')
        if case=='changed-manager': (component/'control').write_bytes(b'altered installed manager')
        if case=='changed-startup': (root/'etc/init.d/cc2-camera-launch').write_bytes(b'foreign startup')
        if case=='changed-manifest': (component/'startup.sha256').write_text(manifest+'unexpected\n')
        if case=='symlink-dhcp':
            dhcp.rename(dhcp.with_suffix('.real'))
            dhcp.symlink_to(dhcp.with_suffix('.real'))
        if case=='symlink-parent':
            parent=dhcp.parent
            parent.rename(parent.with_name('wifi.real'))
            parent.symlink_to(parent.with_name('wifi.real'), target_is_directory=True)
        if case=='empty-rfid':
            for path in rfid.iterdir(): path.unlink()
        if case=='postcheck-rollback':
            stub('sha256sum', f'if [ "$1" = -c ] && [ "$2" = "{component}/startup.sha256" ] && [ "$({busybox} sha256sum "{dhcp}" | {busybox} cut -d " " -f 1)" = "{sha(NEW)}" ]; then exit 1; fi\nexec "{busybox}" sha256sum "$@"')
        text=SCRIPT.read_text()
        for key, value in [('SOURCE',source),('ROOT',component),('DHCP',dhcp),('RFID',rfid),('BASE',root/'opt/usr')]:
            start=next(line for line in text.splitlines() if line.startswith(key+'='))
            text=text.replace(start,key+'='+str(value),1)
        text=text.replace('PATH=/usr/sbin:/usr/bin:/sbin:/bin',f'PATH={bindir}:/usr/sbin:/usr/bin:/sbin:/bin')
        text=text.replace('STARTUP=1b4aeec37ceb49b27f0ea2f6aaba264c4a8a236ce36813db060efaef90c9d380','STARTUP='+sha(manifest.encode()))
        text=text.replace('$2!="/opt/usr/wifi/udhcpc_wlan0"',f'$2!="{dhcp}"')
        script=root/'repair.sh'
        script.write_text(text)
        def snapshot():
            return {str(p.relative_to(root)): (p.read_bytes(), p.stat().st_mode & 0o777)
                    for p in root.rglob('*') if p.is_file() and not p.is_symlink()
                    and not any(part.startswith('cc2-network-dhcp-repair.') for part in p.parts)}
        before=snapshot()
        action='--check' if case.endswith('check') else '--apply'
        result=subprocess.run([busybox,'sh',str(script),action],capture_output=True,text=True)
        expected=case in ('healthy-check','healthy-apply','recognised-check','recognised-apply')
        assert (result.returncode==0)==expected, (case,result.returncode,result.stdout,result.stderr)
        after=snapshot()
        if case=='recognised-apply':
            assert dhcp.read_bytes()==NEW
            before['opt/usr/wifi/udhcpc_wlan0']=(NEW,0o755)
            audits=list((root/'opt/usr').glob('cc2-network-dhcp-repair.*'))
            assert len(audits)==1 and (audits[0]/'dhcp.original').read_bytes()==OLD
            repeat=subprocess.run([busybox,'sh',str(script),'--apply'],capture_output=True,text=True)
            assert repeat.returncode==0 and 'DHCP_ALREADY_ALIGNED_NO_CHANGES' in repeat.stdout
            assert list((root/'opt/usr').glob('cc2-network-dhcp-repair.*'))==audits
        assert before==after, (case, 'unexpected file or permission change')
        if case.startswith('healthy'): assert not list((root/'opt/usr').glob('cc2-network-dhcp-repair.*'))
        print('PASS: BusyBox DHCP repair '+case, flush=True)
print('DHCP_REPAIR_OFFLINE_PASSED: Wi-Fi-only, no-op, check, apply, backup, repeat, refusals and rollback; RFID/config/calibration sentinels unchanged')
