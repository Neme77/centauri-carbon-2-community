"""Actual firmware bootstrap on fake files, never a printer or network stack."""
from pathlib import Path
import hashlib
import importlib.util
import os
import re
import shutil
import subprocess
import tempfile
import unittest

BASE=Path(__file__).resolve().parents[1]
spec=importlib.util.spec_from_file_location('firmware_connections',BASE/'core/connections_component.py')
c=importlib.util.module_from_spec(spec);spec.loader.exec_module(c)
sha=lambda data:hashlib.sha256(data).hexdigest()

class ConnectionsInputTests(unittest.TestCase):
    def test_release_pointer_and_independent_binary_pins(self):
        import json
        pointer=json.loads((c.REPO/'experimental/network-connections/runtime.json').read_text())
        self.assertEqual(pointer['sha256'],c.RUNTIME_SHA256)
        self.assertEqual(pointer['helper_sha256'],c.BINARY_PINS['control'])
        self.assertEqual(len([n for n in c.BINARY_PINS if n.endswith('.ko')]),4)

    def test_changed_or_missing_archive_refused(self):
        with tempfile.TemporaryDirectory() as temporary:
            path=Path(temporary)/'runtime.tar.gz'
            with self.assertRaisesRegex(RuntimeError,'Missing'):c.read_runtime(path)
            path.write_bytes(b'not the reviewed archive')
            with self.assertRaisesRegex(RuntimeError,'SHA256'):c.read_runtime(path)

    def test_bootstrap_has_no_module_service_heater_or_motion_actions(self):
        text=(BASE/'components/connections/firmware-seed.sh').read_text()
        self.assertEqual(text.count('"$ROOT/control" --seed'),1)
        for action in ('insmod ', 'rmmod ', 'killall ', 'reboot\n', 'M112', 'M104', 'M140', 'ip link', 'udhcpc -'):
            self.assertNotIn(action,text)

@unittest.skipIf(os.name=='nt','actual dash/BusyBox fixtures run in the offline Linux check')
class ConnectionsBootstrapTests(unittest.TestCase):
    def fixture(self,shell,case):
        with tempfile.TemporaryDirectory() as temporary:
            base=Path(temporary);usr=base/'opt/usr';source=base/'opt/inst/cc2-connections'
            root=usr/'cc2-network-manager';dhcp=usr/'wifi/udhcpc_wlan0'
            proc=base/'proc';proc.mkdir();source.mkdir(parents=True);dhcp.parent.mkdir(parents=True)
            (base/'tmp').mkdir();(usr/'cc2-network-state').mkdir()
            state=usr/'cc2-network-state/config.bin';state.write_bytes(b'preserve-confirmed-preference')
            trace=base/'trace'
            native=b'native DHCP';wan=b'native WAN';delegate=b'firmware managed DHCP'
            source.joinpath('control').write_text('#!/bin/sh\nprintf "%s\\n" "$*" >> "$TRACE"\n[ "$*" = --seed ] || exit 99\n[ "${FAIL_SEED:-0}" = 0 ]\n')
            source.joinpath('control').chmod(0o700)
            source.joinpath('wifi-client.sh').write_bytes(delegate)
            source.joinpath('native-udhcpc_wlan0').write_bytes(native)
            source.joinpath('native-set_lan.sh').write_bytes(wan)
            source.joinpath('payload').write_bytes(b'qualified fixture')
            startup=base/'startup';startup.write_bytes(b'qualified firmware hooks')
            source.joinpath('startup.sha256').write_text(f'{sha(delegate)}  {dhcp}\n{sha(startup.read_bytes())}  {startup}\n')
            dhcp.write_bytes(native);dhcp.chmod(0o775)
            (proc/'mounts').write_text(f'device {usr} ext4 rw 0 0\n')
            script=(BASE/'components/connections/firmware-seed.sh').read_text()
            replacements={'/opt/usr':str(usr),'/opt/inst':str(base/'opt/inst'),'/proc/':str(proc)+'/', '/tmp/':str(base/'tmp')+'/'}
            # A replacement itself contains /tmp on Linux. Substitute original
            # tokens once; never rewrite a path that was already expanded.
            script=re.sub(r'/opt/usr|/opt/inst|/proc/|/tmp/',lambda match:replacements[match[0]],script)
            script=script.replace('b76f68e345f1221d835d1a0d6279827c5df4823c2e7fadfe0298cd49ec7bb7df',sha(native)).replace('53cf6af55ad333d5781b0d48d89fd48bde22bc516bd22e76d45f098dc224443c',sha(wan))
            env=dict(os.environ,TRACE=str(trace),FAIL_SEED='1' if case=='seed-failure' else '0')
            old=case in ('retained','seed-failure','component-move-failure','dhcp-move-failure','retained-damage','mapped','running')
            if old:
                root.mkdir();root.joinpath('native-udhcpc_wlan0').write_bytes(native);root.joinpath('native-set_lan.sh').write_bytes(wan)
                root.joinpath('control').write_text('#!/bin/sh\necho OLD_COMPONENT_MUST_NOT_RUN >> "$TRACE"\nexit 90\n');root.joinpath('control').chmod(0o700)
                root.joinpath('old-state').write_bytes(b'old component retained')
                root.joinpath('SHA256SUMS').write_text(''.join(f'{sha(p.read_bytes())}  {p.name}\n' for p in sorted(root.iterdir())))
                dhcp.write_bytes(delegate);dhcp.chmod(0o775)
            before={p.name:p.read_bytes() for p in root.iterdir()} if root.exists() else None
            old_dhcp=dhcp.read_bytes();old_mode=dhcp.stat().st_mode&0o777
            if case=='foreign-dhcp':dhcp.write_bytes(b'foreign DHCP');old_dhcp=dhcp.read_bytes()
            if case=='retained-damage':root.joinpath('old-state').write_bytes(b'changed');before['old-state']=b'changed'
            if case in ('running','mapped'):
                process=proc/'12';process.mkdir()
                if case=='running':process.joinpath('exe').symlink_to(root/'control')
                process.joinpath('maps').write_text(str(root/'library.so') if case=='mapped' else '')
            # Fail real file operations at the precise commit boundary.
            bins=base/'bin';bins.mkdir()
            bins.joinpath('pidof').write_text('#!/bin/sh\nexit '+('0' if case=='native-running' else '1')+'\n')
            bins.joinpath('pidof').chmod(0o755)
            for tool in ('cp','mv'):
                content='#!/bin/sh\n'
                if tool=='cp' and case=='copy-failure':content+='case "$*" in *".cc2-firmware-connections-"*) exit 74;;esac\n'
                if tool=='mv' and case=='component-move-failure':content+=f'case "$1" in {usr}/.cc2-firmware-connections-*) exit 74;;esac\n'
                if tool=='mv' and case=='dhcp-move-failure':content+=f'[ "$2" != "{dhcp}" ] || exit 74\n'
                content+=f'exec {shutil.which(tool)} "$@"\n'
                bins.joinpath(tool).write_text(content);bins.joinpath(tool).chmod(0o755)
            script=script.replace('PATH=/usr/sbin:/usr/bin:/sbin:/bin','PATH='+str(bins)+':/usr/sbin:/usr/bin:/sbin:/bin')
            source.joinpath('SHA256SUMS').write_text(''.join(f'{sha(p.read_bytes())}  {p.name}\n' for p in sorted(source.iterdir())))
            if case=='damaged-source':source.joinpath('payload').write_bytes(b'changed')
            file=base/'boot.sh';file.write_text(script)
            result=subprocess.run([*shell,str(file)],env=env,text=True,capture_output=True,timeout=15)
            success=case in ('fresh','retained','idempotent')
            self.assertEqual(result.returncode==0,success,(shell,case,result.stdout,result.stderr))
            reasons={'foreign-dhcp':'foreign_persistent_dhcp','retained-damage':'retained_component_checksum',
                     'damaged-source':'firmware_checksums','mapped':'component_mapped','running':'component_running',
                     'copy-failure':'stage_copy','component-move-failure':'component_commit',
                     'dhcp-move-failure':'dhcp_commit','seed-failure':'configuration_seed',
                     'native-running':'native_services_already_started'}
            if not success:
                self.assertIn('CC2_CONNECTIONS_FIRMWARE_STOP='+reasons[case],result.stderr)
            self.assertEqual(state.read_bytes(),b'preserve-confirmed-preference')
            lines=trace.read_text().splitlines() if trace.exists() else []
            self.assertTrue(all(line=='--seed' for line in lines),(case,lines))
            if success:
                self.assertEqual(dhcp.read_bytes(),delegate)
                self.assertEqual(dhcp.stat().st_mode&0o777,0o755)
                self.assertEqual(root.joinpath('firmware-payload.sha256').read_text().strip(),sha(source.joinpath('SHA256SUMS').read_bytes()))
                if old:self.assertTrue(list(usr.glob('cc2-network-firmware-backup-*/component/old-state')))
                if case=='idempotent':
                    result=subprocess.run([*shell,str(file)],env=env,text=True,capture_output=True,timeout=15)
                    self.assertEqual(result.returncode,0,result.stderr)
                    self.assertEqual(trace.read_text().splitlines(),['--seed'])
            else:
                self.assertEqual(dhcp.read_bytes(),old_dhcp)
                self.assertEqual(dhcp.stat().st_mode&0o777,old_mode)
                if before is not None:
                    self.assertEqual({p.name:p.read_bytes() for p in root.iterdir()},before)
                else:self.assertFalse(root.exists())

    def test_actual_bootstrap_success_refusal_and_rollback(self):
        shells=[['dash'],['busybox','sh']]
        for shell in shells:
            self.assertIsNotNone(shutil.which(shell[0]),'offline image must provide both shells')
            for case in ('fresh','retained','idempotent','foreign-dhcp','retained-damage','damaged-source','mapped','running','native-running','copy-failure','component-move-failure','dhcp-move-failure','seed-failure'):
                with self.subTest(shell=shell,case=case):self.fixture(shell,case)

if __name__=='__main__':unittest.main()
