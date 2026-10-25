"""Policy and archive tests; Linux also executes the real transaction in an isolated fixture."""
import io
import json
import os
import re
from pathlib import Path
import subprocess
import sys
import tarfile
import tempfile
import unittest
from unittest.mock import patch
import manage
import policy
import patch_pullback

class PolicyTests(unittest.TestCase):
    def test_clamp_preserves_other_settings(self):
        source=b'[other]\r\nunload_fila_dist=999\r\n[canvas_dev]\r\nunload_fila_dist = 5.6e2 # custom\r\nmesh = unchanged\r\n#*# [canvas_dev]\r\n#*# unload_fila_dist: 1000\r\n'
        out, values=policy.clamp_config(source)
        self.assertEqual(out,source.replace(b'5.6e2',b'350').replace(b': 1000',b': 350'))
        self.assertEqual(values,[560,1000])
    def test_reject_ambiguous_config(self):
        for value in (b'nan',b'inf',b'-60',b'59',b'1001',b'350 garbage',b'1e999999'):
            with self.subTest(value=value),self.assertRaises(ValueError):policy.clamp_config(b'[canvas_dev]\nunload_fila_dist='+value+b'\n')
        for source in (b'[canvas_dev\nx=2',b'[include other.cfg]\n',b'\0'):
            with self.assertRaises(ValueError):policy.clamp_config(source)
    def test_boundary_values_unchanged(self):
        for value in (b'60',b'350',b'+350.0',b'3.5e2'):
            source=b'[canvas_dev]\nunload_fila_dist='+value+b'\n'
            self.assertEqual(policy.clamp_config(source)[0],source)
    def test_patch_rejects_other_input(self):
        with self.assertRaises(ValueError):patch_pullback.transform(b'unknown')
    def test_qualified_private_transform_if_available(self):
        source=os.environ.get('CC2_PRIVATE_ELF')
        if not source:self.skipTest('private vendor input intentionally not distributed')
        original=Path(source).read_bytes();extended=patch_pullback.transform(original)
        self.assertEqual(policy.digest(extended),patch_pullback.EXTENDED_SHA256)
        self.assertEqual(patch_pullback.transform(extended,True),original)
        self.assertEqual([i for i,(a,b) in enumerate(zip(original,extended)) if a!=b],[0x5bba3d,0x5bba3e])
        repo=Path(__file__).resolve().parents[2]
        launcher=(repo/'experimental/firmware-port/launcher.sh').read_bytes()
        boot=(repo/'builder/current/components/reactor/firmware-launcher.sh').read_bytes()
        result=policy.patched_files(dict(program=original,launcher=launcher,boot=boot,sums=policy.SUMS))
        self.assertIn(policy.digest(result['launcher']).encode(),result['sums'])
        self.assertIn(b'EXPECTED='+patch_pullback.ORIGINAL_SHA256.encode(),result['boot'])
    def test_archive_rejects_paths_links_duplicates(self):
        for name,kind in (('../outside',tarfile.REGTYPE),('/absolute',tarfile.REGTYPE),('safe',tarfile.SYMTYPE)):
            member=tarfile.TarInfo(name);member.type=kind
            with self.assertRaises(ValueError):policy.validate_tar_member(member)
        payload={'plan':b'a'}
        with self.assertRaises(ValueError):manage.unpack(manage.archive_payload(payload))
    def test_ready_guards(self):
        printer=dict(connected=True,last_message_age=1,machine={'status':1},print={'enabled':False},extruder={'temperature':25,'target':0},heater_bed={'temperature':25,'target':0})
        health=dict(version='1.2.21',mqtt_registered=True)
        def check(p=printer,h=health):manage.ready({'printer.json':json.dumps(p).encode(),'health.json':json.dumps(h).encode()})
        check()
        for changes in ({'last_message_age':16},{'connected':False},{'machine':{'status':2}},{'print':{'enabled':True}},{'heater_bed':{'temperature':51,'target':0}},{'extruder':{'temperature':25,'target':1}}):
            with self.assertRaises(ValueError):check(printer|changes)
        with self.assertRaises(ValueError):check(h=health|{'version':'1.2.19'})
    def test_remote_host_injection_refused(self):
        for host in ('-oProxyCommand=x','x; reboot','user@host','host/path'):
            with self.assertRaises(ValueError):manage.Remote(host)

@unittest.skipIf(os.name=='nt','POSIX transaction fixture runs in Linux CI')
class TransactionTests(unittest.TestCase):
    def setUp(self):
        self.temp=tempfile.TemporaryDirectory();self.root=Path(self.temp.name)
        (self.root/'tmp').mkdir()
        self.stage=self.root/'opt/usr/.cc2-pullback-stage-0123456789abcdefabcd';self.stage.mkdir(parents=True)
        self.state=self.root/'opt/usr/cc2-canvas-pullback-1000'
        self.base=self.root/'opt/usr/cc2-reactor-runtime-v2';self.base.mkdir()
        self.targets={k:self.root/p.lstrip('/') for k,p in policy.TARGETS.items()}
        self.original={k:('old-'+k).encode() for k in self.targets}
        self.extended={k:('new-'+k).encode() for k in self.targets}
        for k,p in self.targets.items():p.parent.mkdir(parents=True,exist_ok=True);p.write_bytes(self.original[k])
        (self.base/'preflight.sh').write_text('exit 0\n')
        self.bin=self.root/'bin';self.bin.mkdir()
        self.script('pidof','echo 42')
        self.script('wget',"case \"$*\" in *health) echo '{\"mqtt_registered\":true}';; *) cat '"+str(self.root/'printer.json')+"';; esac")
        self.script('df','echo "disk 999999 1 999999 1% /opt/usr"')
        self.script('sync','exit 0')
        (self.root/'proc/42').mkdir(parents=True)
        (self.root/'proc/42/stat').write_text('42 (printer) S '+'0 '*18+'1234 0\n')
        self.printer=dict(connected=True,last_message_age=1,machine={'status':1},print={'enabled':False},extruder={'temperature':25,'target':0},heater_bed={'temperature':25,'target':0})
        self.write_printer()
    def tearDown(self):self.temp.cleanup()
    def script(self,name,body):
        p=self.bin/name;p.write_text('#!/bin/sh\n'+body+'\n');p.chmod(0o755)
    def write_printer(self): (self.root/'printer.json').write_text(json.dumps(self.printer,separators=(',',':')))
    def payload(self,mode='install',partial=False,config=False):
        if mode=='remove':
            (self.state/'backup').mkdir(parents=True,exist_ok=True)
            for k,p in self.targets.items():
                (self.state/'backup'/k).write_bytes(self.original[k]);p.write_bytes(self.extended[k] if not partial or k=='boot' else self.original[k])
            order=['program','sums','launcher','boot'];data=self.original
        else:order=list(self.targets);data=self.extended
        plan='';before='';after='';files={}
        if config:
            p=self.root/'opt/usr/cfg/autosave.cfg';p.parent.mkdir(parents=True);p.write_bytes(b'[canvas_dev]\nunload_fila_dist=560\n')
            files['after/autosave']=b'[canvas_dev]\nunload_fila_dist=350\n'
            plan+=f'autosave|{p}|{policy.digest(p.read_bytes())}|{policy.digest(files["after/autosave"])}|600\n'
            before+=policy.digest(p.read_bytes())+'  '+str(p)+'\n';after+=policy.digest(files['after/autosave'])+'  '+str(p)+'\n'
        for k in order:
            p=self.targets[k];files['after/'+k]=data[k]
            plan+=f'{k}|{p}|{policy.digest(p.read_bytes())}|{policy.digest(data[k])}|700\n'
            before+=policy.digest(p.read_bytes())+'  '+str(p)+'\n';after+=policy.digest(data[k])+'  '+str(p)+'\n'
        text=(Path(__file__).parent/'transaction.sh').read_text()
        # Tests rewrite literal absolute paths to a disposable tree. Production has no override.
        text=re.sub(r'/opt/usr|/opt/inst|/etc/init.d|/tmp|/proc',lambda m:str(self.root)+m[0],text)
        files.update({'transaction.sh':text.encode(),'pid':b'42\n','start':b'1234\n','plan':plan.encode(),'before.sha256':before.encode(),'after.sha256':after.encode(),'manifest.json':b'{}\n'})
        for n,b in files.items():p=self.stage/n;p.parent.mkdir(exist_ok=True,parents=True);p.write_bytes(b)
        self.resign()
    def resign(self):
        lines=[]
        for p in self.stage.rglob('*'):
            if p.is_file() and p.name!='payload.sha256':lines.append(policy.digest(p.read_bytes())+'  '+p.relative_to(self.stage).as_posix()+'\n')
        (self.stage/'payload.sha256').write_text(''.join(lines))
    def run_transaction(self,mode='install'):
        env=os.environ|{'PATH':str(self.bin)+':'+os.environ['PATH']}
        return subprocess.run(['sh',str(self.stage/'transaction.sh'),mode],env=env,stdout=subprocess.PIPE,stderr=subprocess.STDOUT,text=True)
    def test_install_and_remove(self):
        self.payload();r=self.run_transaction();self.assertEqual(r.returncode,0,r.stdout)
        for k,p in self.targets.items():self.assertEqual(p.read_bytes(),self.extended[k])
        self.payload('remove',config=True);r=self.run_transaction('remove');self.assertEqual(r.returncode,0,r.stdout)
        for k,p in self.targets.items():self.assertEqual(p.read_bytes(),self.original[k])
        self.assertIn(b'=350', (self.root/'opt/usr/cfg/autosave.cfg').read_bytes())
    def test_partial_install_can_be_removed(self):
        self.payload('remove',partial=True);r=self.run_transaction('remove');self.assertEqual(r.returncode,0,r.stdout)
        for k,p in self.targets.items():self.assertEqual(p.read_bytes(),self.original[k])
    def test_guards_refuse_without_changes(self):
        self.payload();self.printer['last_message_age']=99;self.write_printer()
        r=self.run_transaction();self.assertNotEqual(r.returncode,0);self.assertIn('stale telemetry',r.stdout)
        for k,p in self.targets.items():self.assertEqual(p.read_bytes(),self.original[k])
    def test_process_replaced(self):
        self.payload();(self.root/'proc/42/stat').write_text('42 (printer) S '+'0 '*18+'1235 0\n')
        r=self.run_transaction();self.assertNotEqual(r.returncode,0);self.assertIn('printer process was replaced',r.stdout)
    def test_foreign_target_refused(self):
        self.payload();self.targets['boot'].write_bytes(b'foreign')
        r=self.run_transaction();self.assertNotEqual(r.returncode,0);self.assertIn('files changed while preparing',r.stdout)
        self.assertEqual(self.targets['boot'].read_bytes(),b'foreign')
    def test_plan_cannot_write_unknown_path(self):
        self.payload();p=self.stage/'plan';p.write_text(p.read_text().replace(str(self.targets['boot']),str(self.root/'outside')));self.resign()
        r=self.run_transaction();self.assertNotEqual(r.returncode,0);self.assertIn('invalid transaction target',r.stdout);self.assertFalse((self.root/'outside').exists())
    def test_mid_transaction_failure_restores_original(self):
        self.payload();native=subprocess.check_output(['sh','-c','command -v mv'],text=True).strip()
        self.script('mv',f'case "$*" in *launcher.sh.cc2-pb-new*) exit 1;; esac\nexec {native} "$@"')
        r=self.run_transaction();self.assertNotEqual(r.returncode,0);self.assertIn('Original files restored',r.stdout);self.assertTrue((self.state/'backup/boot').exists())
        for k,p in self.targets.items():self.assertEqual(p.read_bytes(),self.original[k])

if __name__=='__main__':unittest.main(verbosity=2)
