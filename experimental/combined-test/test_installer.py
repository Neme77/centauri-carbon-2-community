#!/usr/bin/env python3
"""Execute the real shell installer in a disposable filesystem with mocked services."""
import hashlib
import json
import os
import re
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

HERE=Path(__file__).resolve().parent
MOCK='''#!/usr/bin/env python3
import json,os,sys
from pathlib import Path
r=Path(os.environ['MOCK_ROOT']);statefile=r/'state.json';s=json.loads(statefile.read_text())
cmd=Path(sys.argv[0]).name
if cmd=='sleep':sys.exit(0)
if cmd=='df':print('Filesystem 1K-blocks Used Available Use% Mounted on\\nmock 1000000 1000 999000 1% /opt/usr');sys.exit(0)
if cmd=='pidof':
    ids=[str({'elegoo_printer':100,'cc2-control':101}[x]) for x in sys.argv[1:] if x in s and s[x]]
    if ids:print(' '.join(ids));sys.exit(0)
    sys.exit(1)
if cmd=='wget':
    path=sys.argv[-1]
    if '/api/printer' in path:print(json.dumps({'connected':True,'machine':{'status':2 if os.environ.get('BUSY') else 1,'sub_status':0},'last_message_age':0},separators=(',',':')))
    elif '/api/uds' in path:print(json.dumps({'connected':True,'fresh':True,'values':{'nozzle_temperature':30,'nozzle_target':0,'bed_temperature':25,'bed_target':0,'live_velocity':0}},separators=(',',':')))
    else:print('{"service":"cc2-control","mqtt_registered":true,"snapshot_received":true}')
    sys.exit(0)
if cmd=='service-ctl':
    name,action,path=sys.argv[1:]
    if action=='enable':sys.exit(0)
    s[name]=action=='start';statefile.write_text(json.dumps(s))
    if name=='elegoo_printer' and action=='start':
        patched='cc2-reactor-launch' in Path(path).read_text() or ('run_printer.sh start' in Path(path).read_text() and 'cc2-reactor-launch' in (r/'opt/bin/run_printer.sh').read_text())
        (r/'proc/100/maps').write_text(str(r/'opt/usr/cc2-reactor-runtime-v2/libcc2-reactor-v2.so') if patched else '')
        if patched and not os.environ.get('FAIL_HOOK'):
            (r/'opt/usr/cc2-reactor-runtime-v2/activation.marker').write_text('pid=100 callback=0x1 timers=0x2 poll=0x3 wait=0x4\\n')
    sys.exit(0)
sys.exit(2)
'''

class InstallerTests(unittest.TestCase):
    def scenario(self,busy=False,fail_hook=False,indirect=False):
        with tempfile.TemporaryDirectory(prefix='cc2-combined-installer-') as temporary:
            root=Path(temporary);stage=root/'stage';stage.mkdir();bins=root/'bin';bins.mkdir()
            for path in ('etc/init.d','opt/bin','opt/usr/cc2-control/web','proc/100','proc/101','tmp'):
                (root/path).mkdir(parents=True,exist_ok=True)
            vendor=root/'opt/bin/elegoo_printer';vendor.write_bytes(b'vendor original')
            vendorhash=hashlib.sha256(vendor.read_bytes()).hexdigest()
            target=root/'opt/usr/cc2-control';(target/'cc2-control').write_bytes(b'old CC2')
            (target/'cc2-control.conf').write_bytes(b'private configuration untouched')
            for pid,path in ((100,vendor),(101,target/'cc2-control')):
                (root/f'proc/{pid}/exe').symlink_to(path)
                (root/f'proc/{pid}/maps').write_text('')
            (root/'proc/meminfo').write_text('MemAvailable: 33000 kB\n')
            (root/'proc/mounts').write_text('root / rootfs rw 0 0\n')
            (root/'state.json').write_text(json.dumps({'elegoo_printer':True,'cc2-control':True}))
            for cmd in ('pidof','sleep','wget','df','service-ctl'):
                path=bins/cmd;path.write_text(MOCK);path.chmod(0o755)
            service='''#!/bin/sh
case "$1" in
 start)
   service-ctl elegoo_printer start "$0"
   if false; then
     elegoo_printer &
   fi
 ;;
 stop) service-ctl elegoo_printer stop "$0";;
esac
'''
            ccinit='#!/bin/sh\nservice-ctl cc2-control "$1" "$0"\n'
            if indirect:
                service=service.replace('     elegoo_printer &','     run_printer.sh start &')
            printer=root/'etc/init.d/printer';printer.write_text(service);printer.chmod(0o755)
            launch=printer
            original_launch=service
            if indirect:
                launch=root/'opt/bin/run_printer.sh'
                original_launch='#!/bin/sh\nstart(){\n LD_BIND_NOW=1 elegoo_printer $DEF_CFG_DIR/$CFG_FILE -s $USR_CFG_DIR/$AUTO_SAVE_CFG_FILE -a $UDS_FILE&\n}\nstart_product_test(){\n exec elegoo_printer product.cfg\n}\n'
                launch.write_text(original_launch);launch.chmod(0o755)
            init=root/'etc/init.d/cc2-control';init.write_text(ccinit);init.chmod(0o755)
            newbinary=b'new CC2';newhash=hashlib.sha256(newbinary).hexdigest()
            for name in ('install-combined.sh','restore-combined.sh','common.sh','boot-launcher.sh'):
                text=(HERE/name).read_text().replace('@CC2_SHA256@',newhash).replace(
                    'c21126e78a63ac9140e7347c56f363d5e513495c58c06fc17e8ef97846f3c0a3',vendorhash)
                text=re.sub(r'/(opt|etc|proc|tmp)/',lambda m:str(root)+m.group(0),text)
                (stage/name).write_text(text)
            (stage/'cc2-control').write_bytes(newbinary)
            (stage/'cc2-uds-probe').write_bytes(b'probe')
            for name in ('start.sh','launch.sh'):(stage/name).write_text('#!/bin/sh\nexit 0\n')
            (stage/'cc2-control.init').write_text(ccinit)
            (stage/'build-info.json').write_text('{}')
            (stage/'web/locales').mkdir(parents=True)
            (stage/'web/index.html').write_text('new web')
            (stage/'web/locales/en.json').write_text('{}')
            (stage/'reactor').mkdir()
            for name in ('launcher.sh','cc2-reactor-bridge-test','libcc2-reactor-v2.so'):
                (stage/'reactor'/name).write_text('test fixture')
            (stage/'reactor/preflight.sh').write_text('#!/bin/sh\nexit 0\n')
            (stage/'reactor/SHA256SUMS').write_text(''.join(hashlib.sha256(p.read_bytes()).hexdigest()+'  '+p.name+'\n'
                for p in sorted((stage/'reactor').iterdir())))
            (stage/'SHA256SUMS').write_text(''.join(hashlib.sha256(p.read_bytes()).hexdigest()+'  '+p.relative_to(stage).as_posix()+'\n'
                for p in sorted(stage.rglob('*')) if p.is_file()))
            env={**os.environ,'PATH':str(bins)+':'+os.environ['PATH'],'MOCK_ROOT':str(root)}
            if busy:env['BUSY']='1'
            if fail_hook:env['FAIL_HOOK']='1'
            result=subprocess.run(['sh',str(stage/'install-combined.sh')],env=env,capture_output=True,text=True)
            self.assertEqual((target/'cc2-control.conf').read_bytes(),b'private configuration untouched')
            if busy or fail_hook:
                self.assertNotEqual(result.returncode,0,result.stdout+result.stderr)
                if fail_hook:self.assertIn('Installazione fallita',result.stdout)
                if busy:self.assertIn('STOP: attendere macchina inattiva',result.stdout)
                self.assertEqual(printer.read_text(),service)
                self.assertEqual(launch.read_text(),original_launch)
                self.assertEqual((target/'cc2-control').read_bytes(),b'old CC2')
                self.assertFalse((root/'etc/init.d/cc2-reactor-launch').exists())
                self.assertEqual(json.loads((root/'state.json').read_text()),{'elegoo_printer':True,'cc2-control':True})
            else:
                self.assertEqual(result.returncode,0,result.stdout+result.stderr)
                self.assertIn('INSTALLAZIONE COMBINATA PASS',result.stdout)
                self.assertIn('cc2-reactor-launch',launch.read_text())
                if indirect:
                    self.assertEqual(printer.read_text(),service)
                    self.assertIn('LD_BIND_NOW=1 ',launch.read_text())
                    self.assertIn('exec elegoo_printer product.cfg',launch.read_text())
                self.assertEqual((target/'cc2-control').read_bytes(),newbinary)
                (target/'cc2-control.conf').write_bytes(b'LAN configuration renewed during test')
                backup=Path((stage/'backup.path').read_text().strip())
                restored=subprocess.run(['sh',str(backup/'restore.sh')],env=env,capture_output=True,text=True)
                self.assertEqual(restored.returncode,0,restored.stdout+restored.stderr)
                self.assertEqual(printer.read_text(),service)
                self.assertEqual(launch.read_text(),original_launch)
                self.assertEqual((target/'cc2-control').read_bytes(),b'old CC2')
                self.assertEqual((target/'cc2-control.conf').read_bytes(),b'LAN configuration renewed during test')
                self.assertFalse((root/'etc/init.d/cc2-reactor-launch').exists())
    def test_install_and_restore_preserve_config(self):self.scenario()
    def test_indirect_launcher_install_restore(self):self.scenario(indirect=True)
    def test_indirect_launcher_rollback(self):self.scenario(indirect=True,fail_hook=True)
    def test_busy_stops_before_service_changes(self):self.scenario(busy=True)
    def test_failed_hook_restores_services_and_files(self):self.scenario(fail_hook=True)
if __name__=='__main__':unittest.main()
