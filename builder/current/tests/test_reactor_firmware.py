"""Firmware callback seeding, fallback, provenance and launch-line regressions."""
import hashlib
import importlib.util
import json
from pathlib import Path
import struct
import subprocess
import tempfile
import unittest
from unittest.mock import patch

BUILDER=Path(__file__).resolve().parents[1]
REPO=BUILDER.parents[1]
spec=importlib.util.spec_from_file_location('reactor',BUILDER/'core/reactor_component.py')
reactor=importlib.util.module_from_spec(spec);spec.loader.exec_module(reactor)
sha=lambda data:hashlib.sha256(data).hexdigest()

class FirmwareReactorTests(unittest.TestCase):
    def test_preserves_vendor_arguments_and_special_branches(self):
        original='#!/bin/sh\nif [ "$MODE" = normal ]; then\n LD_BIND_NOW=1 elegoo_printer "$CFG" -s "$AUTO" -a "$UDS"&\nelse\n elegoo_printer_debug "$CFG"\nfi\n'
        patched=reactor.patched_vendor_launcher(original)
        self.assertIn('LD_BIND_NOW=1 /etc/init.d/cc2-reactor-launch "$CFG" -s "$AUTO" -a "$UDS"&',patched)
        self.assertIn('elegoo_printer_debug "$CFG"',patched)
        for text in ('#!/bin/sh\necho nothing\n',original+'elegoo_printer another\n'):
            with self.assertRaises(RuntimeError):reactor.patched_vendor_launcher(text)

    def test_component_provenance_and_corruption(self):
        with tempfile.TemporaryDirectory() as temporary:
            component=Path(temporary);prepared=component/'prepared';prepared.mkdir()
            (component/'firmware-launcher.sh').write_bytes((BUILDER/'components/reactor/firmware-launcher.sh').read_bytes())
            data=bytearray(64);data[:6]=b'\x7fELF\x01\x01';struct.pack_into('<H',data,18,40)
            for name in reactor.FILES[:2]:(prepared/name).write_bytes(data)
            for name in ('launcher.sh','preflight.sh'):(prepared/name).write_bytes((REPO/'experimental/combined-test'/name).read_bytes())
            hashes={name:reactor.digest(prepared/name) for name in reactor.FILES[:-1]}
            (prepared/'SHA256SUMS').write_text(''.join(hashes[name]+'  '+name+'\n' for name in sorted(hashes)))
            hashes['SHA256SUMS']=reactor.digest(prepared/'SHA256SUMS')
            manifest={'optimization':'O0','gcc':'6.5.0','glibc':'2.23','vendor_sha256':reactor.VENDOR_SHA256,'libco_sha256':reactor.LIBCO_SHA256,
                'files':hashes,'sources':{name:reactor.digest(REPO/'experimental/combined-test/reactor'/name) for name in ('reactor_module.cpp','bridge_test.cpp','co_routine.h','fingerprints.h')}}
            path=component/'prepared-manifest.json';path.write_text(json.dumps(manifest))
            reactor.load(component)
            root=component/'rootfs'
            for directory in ('opt/bin','opt/lib','etc/init.d'):(root/directory).mkdir(parents=True,exist_ok=True)
            (root/'opt/bin/elegoo_printer').write_bytes(b'fixture vendor')
            (root/'opt/lib/libcolib.so').write_bytes(b'fixture libco')
            original='#!/bin/sh\nLD_BIND_NOW=1 elegoo_printer "$CFG" -s "$AUTO" -a "$UDS"&\n'
            (root/'opt/bin/run_printer.sh').write_text(original)
            (root/'etc/init.d/printer').write_text('start(){\n    if [ $ec_gui_exist_cmd ];then\n        ec-eeb001-gui &\n    fi\n    sleep 8\n    run_printer.sh start &\n}\n')
            real_digest=reactor.digest
            def fixture_digest(p):
                if Path(p)==root/'opt/bin/elegoo_printer':return reactor.VENDOR_SHA256
                if Path(p)==root/'opt/lib/libcolib.so':return reactor.LIBCO_SHA256
                return real_digest(p)
            with patch.object(reactor,'digest',fixture_digest):
                reactor.install(root,component);reactor.audit(root,component)
                self.assertEqual((root/'opt/inst/cc2-reactor/run_printer.original.sh').read_text(),original)
                (root/'etc/init.d/cc2-reactor-launch').write_text('corrupted')
                with self.assertRaises(RuntimeError):reactor.audit(root,component)
            for key,value in [('optimization','O1'),('gcc','15.2.0'),('vendor_sha256','bad')]:
                changed=dict(manifest);changed[key]=value;path.write_text(json.dumps(changed))
                with self.assertRaises(RuntimeError):reactor.load(component)
            path.write_text(json.dumps(manifest));(prepared/'libcc2-reactor-v2.so').write_bytes(b'corrupted')
            with self.assertRaises(RuntimeError):reactor.load(component)

    def test_prepare_before_gui_preserves_vendor_order(self):
        original='start(){\n    factory_check\n    if [ $ec_gui_exist_cmd ];then\n        ec-eeb001-gui &\n    fi\n    ai_camera &\n    sleep 8\n    run_printer.sh start &\n}\n'
        result=reactor.patched_printer_init(original)
        self.assertLess(result.index('--prepare-only'),result.index('ec-eeb001-gui &'))
        self.assertEqual(result.split('\n\n',1)[1],original[original.index('    if '):])
        for bad in ('nothing',original+original,result):
            with self.assertRaises(RuntimeError):reactor.patched_printer_init(bad)

    def boot(self,scenario):
        with tempfile.TemporaryDirectory() as temporary:
            root=Path(temporary);source=root/'opt/inst/cc2-reactor';base=root/'opt/usr/cc2-reactor-runtime-v2'
            source.mkdir(parents=True);base.parent.mkdir(parents=True);(root/'opt/bin').mkdir();(root/'opt/lib').mkdir()
            vendor=root/'opt/bin/elegoo_printer'
            vendor.write_text('#!/bin/sh\nprintf "VENDOR:%s:%s\\n" "$LD_BIND_NOW" "$*"\n');vendor.chmod(0o755)
            lib=root/'opt/lib/libcolib.so';lib.write_bytes(b'fixture-libco')
            preflight='#!/bin/sh\nexit '+('1' if scenario=='preflight-fails' else '0')+'\n'
            launcher='#!/bin/sh\necho CALLBACK\nexec '+str(vendor)+' "$@"\n'
            for name,data in [('preflight.sh',preflight.encode()),('launcher.sh',launcher.encode()),('libcc2-reactor-v2.so',b'fixture'),('cc2-reactor-bridge-test',b'fixture')]:
                (source/name).write_bytes(data)
            sums=''.join(sha(p.read_bytes())+'  '+p.name+'\n' for p in sorted(source.iterdir()))
            (source/'SHA256SUMS').write_text(sums)
            mounts=root/'mounts';mounts.write_text('device '+str(base.parent)+' ext4 rw 0 0\n')
            if scenario=='disabled':(base.parent/'cc2-reactor-disabled').touch()
            if scenario=='wrong-libco':lib.write_bytes(b'wrong')
            if scenario=='corrupt-payload':(source/'launcher.sh').write_text('changed')
            if scenario in ('replace-old','preflight-fails'):
                base.mkdir();(base/'keep-backup').write_text('old backup pointer')
            script=(BUILDER/'components/reactor/firmware-launcher.sh').read_text()
            script=script.replace('/opt/',str(root)+'/opt/').replace('/proc/mounts',str(mounts))
            script=script.replace(reactor.VENDOR_SHA256,sha(vendor.read_bytes())).replace(reactor.LIBCO_SHA256,sha(b'fixture-libco'))
            script=script.replace('logger -t cc2-reactor','echo')
            path=root/'boot.sh';path.write_text(script)
            env=dict(__import__('os').environ,LD_BIND_NOW='1');env.pop('LD_PRELOAD',None)
            preparation=subprocess.run(['sh',str(path),'--prepare-only'],env=env,text=True,capture_output=True)
            self.assertEqual(preparation.returncode,0,preparation.stderr)
            self.assertNotIn('VENDOR:',preparation.stdout)
            self.assertNotIn('CALLBACK',preparation.stdout)
            result=subprocess.run(['sh',str(path),'config','-s','autosave','-a','uds'],env=env,text=True,capture_output=True)
            self.assertEqual(result.returncode,0,result.stderr)
            self.assertIn('VENDOR:1:config -s autosave -a uds',result.stdout)
            if scenario in ('fresh','replace-old'):
                self.assertIn('CALLBACK',result.stdout);self.assertTrue((base/'firmware-payload.sha256').exists())
                if scenario=='replace-old':
                    self.assertEqual(next(base.parent.glob('cc2-reactor-before-firmware-*')).joinpath('keep-backup').read_text(),'old backup pointer')
                second=subprocess.run(['sh',str(path),'config'],env=env,text=True,capture_output=True)
                self.assertEqual(second.returncode,0,second.stderr);self.assertIn('CALLBACK',second.stdout)
            else:
                self.assertNotIn('CALLBACK',result.stdout)
                if scenario=='preflight-fails':self.assertEqual((base/'keep-backup').read_text(),'old backup pointer')

    def test_seed_and_idempotent_reboot(self):self.boot('fresh')
    def test_preserves_previous_runtime(self):self.boot('replace-old')
    def test_disabled_starts_vendor(self):self.boot('disabled')
    def test_incompatible_libco_starts_vendor(self):self.boot('wrong-libco')
    def test_corrupt_payload_starts_vendor(self):self.boot('corrupt-payload')
    def test_failed_native_preflight_keeps_old_runtime(self):self.boot('preflight-fails')

if __name__=='__main__':unittest.main()
