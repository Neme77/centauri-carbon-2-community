"""First firmware boot, managed upgrades, rollback and fail-closed camera fixtures."""
from pathlib import Path
import hashlib
import importlib.util
import os
import re
import shlex
import shutil
import subprocess
import tempfile
import unittest
from unittest.mock import patch

BUILDER=Path(__file__).resolve().parents[1]
spec=importlib.util.spec_from_file_location('firmware_camera',BUILDER/'core/camera_component.py')
camera=importlib.util.module_from_spec(spec);spec.loader.exec_module(camera)
sha=lambda b:hashlib.sha256(b).hexdigest()

class CameraFirmwareTests(unittest.TestCase):
    def test_canonical_camera_shell_sources_are_lf(self):
        for path in [BUILDER/'components/camera/firmware-launcher.sh',*(camera.REPO/'cc2-control/installer/camera'/name for name in ('common.sh','manage.sh','boot-camera.sh'))]:
            data=path.read_bytes()
            self.assertTrue(data.startswith(b'#!/bin/sh\n'),str(path))
            self.assertNotIn(b'\r',data,str(path))
    @unittest.skipUnless((BUILDER/'components/camera/prepared-manifest.json').is_file(),'qualified runtime supplied in builder packaging workflow')
    def test_rootfs_install_and_reextraction_audit(self):
        with tempfile.TemporaryDirectory() as temporary:
            root=Path(temporary)
            (root/'etc/init.d').mkdir(parents=True);(root/'opt/bin').mkdir(parents=True)
            original=b'#!/bin/sh\nstart(){\n    ai_camera &\n}\n'
            init=root/'etc/init.d/printer';init.write_bytes(original)
            vendor=root/'opt/bin/ai_camera';vendor.write_bytes(b'qualified-vendor-fixture')
            real_sha=camera.sha
            def fixture_sha(path):
                path=Path(path)
                if path==vendor:return camera.CAMERA_SHA
                if path.is_file() and path.read_bytes()==original:return camera.INIT_SHA
                return real_sha(path)
            component=BUILDER/'components/camera'
            with patch.object(camera,'sha',fixture_sha):
                camera.install(root,component);camera.audit(root,component)
                self.assertEqual((root/'opt/inst/cc2-camera-r2/printer.original').read_bytes(),original)
                hook=root/'etc/init.d/cc2-camera-launch';saved=hook.read_bytes();hook.write_bytes(b'changed')
                with self.assertRaises(RuntimeError):camera.audit(root,component)
                hook.write_bytes(saved);init.write_bytes(original)
                with self.assertRaises(RuntimeError):camera.audit(root,component)

    def test_launch_transform_preserves_other_services(self):
        original='#!/bin/sh\nstart(){\n    gui &\n    ai_camera &\n    mosquitto &\n}\n'
        changed=camera.patched_init(original)
        self.assertIn('    gui &\n',changed);self.assertIn('    mosquitto &\n',changed)
        self.assertEqual(changed.count('CC2_CAMERA_FIRMWARE_R2'),1)
        for bad in ('nothing',original+original,changed):
            with self.assertRaises(RuntimeError):camera.patched_init(bad)

    def boot(self,scenario):
        with tempfile.TemporaryDirectory() as temporary:
            root=Path(temporary);source=root/'opt/inst/cc2-camera-r2';base=root/'opt/usr/cc2-control/camera'
            for directory in ('opt/inst/cc2-camera-r2','opt/usr','opt/bin','etc/init.d','tmp','bin'):(root/directory).mkdir(parents=True,exist_ok=True)
            vendor=root/'opt/bin/ai_camera'
            vendor.write_text('#!/bin/sh\nprintf "CAMERA:%s:%s:%s\\n" "$CC2_CAMERA_REAP_TRIAL" "$CC2_CAMERA_RECOVERY_TRIAL" "$*"\n');vendor.chmod(0o755)
            vendor_sha=sha(vendor.read_bytes())
            original=b'#!/bin/sh\nai_camera &\n'
            init=root/'etc/init.d/printer';init.write_text(camera.patched_init(original.decode()))
            mounts=root/'mounts';mounts.write_text('device '+str(root/'opt/usr')+' ext4 rw 0 0\n')
            stub=root/'bin/pidof';stub.write_text('#!/bin/sh\nexit 1\n');stub.chmod(0o755)
            (source/'printer.original').write_bytes(original)
            (source/'patched.sha256').write_text(sha(init.read_bytes())+'\n')
            # Shell fixtures exercise actual boot/management paths, with only
            # native symbols/telemetry substituted; no camera hardware is used.
            common=(camera.REPO/'cc2-control/installer/camera/common.sh').read_text()
            common=common.replace(camera.CAMERA_SHA,sha(vendor.read_bytes())).replace(camera.INIT_SHA,sha(original))
            common+='\ndependencies() { return 0; }\nruntime_check() { return '+('1' if scenario=='runtime-fails' else '0')+'; }\nidle() { return 0; }\n'
            values={'common.sh':common.encode(),'manage.sh':(camera.REPO/'cc2-control/installer/camera/manage.sh').read_bytes(),
                    'boot-camera.sh':(camera.REPO/'cc2-control/installer/camera/boot-camera.sh').read_bytes(),
                    'libcc2-camera-reap-r2.so':b'fixture-reap','libcc2-camera-recovery-r2.so':b'fixture-recovery','cc2-camera-reap-check':b'fixture-helper'}
            for name,pin in camera.payload.PINS.items():
                values['common.sh']=values['common.sh'].replace(pin.encode(),sha(values[name]).encode())
            for name in ('common.sh','manage.sh','boot-camera.sh'):
                values[name]=re.sub(rb'/(?:opt|etc|tmp)/',lambda m:str(root).encode()+m[0],values[name])
            for name,data in values.items():(source/name).write_bytes(data);(source/name).chmod(0o700)
            (source/'SHA256SUMS').write_text(''.join(sha(data)+'  '+name+'\n' for name,data in sorted(values.items())))
            if scenario.startswith('legacy') or scenario in ('disabled','unknown-existing','symlink'):
                base.mkdir(parents=True)
                for name in (*values,'SHA256SUMS'):shutil.copyfile(source/name,base/name);(base/name).chmod(0o700)
                (base/'printer.original').write_bytes(original)
                if scenario.startswith('legacy'):
                    old=original.decode().replace('ai_camera &',values['common.sh'].decode().split("NEW_LINE='",1)[1].split("'",1)[0])
                    (base/'patched.sha256').write_text(sha(old.encode())+'\n')
                else:(base/'patched.sha256').write_bytes((source/'patched.sha256').read_bytes())
                if scenario not in ('disabled','legacy-disabled'):(base/'enabled').write_text('CAMERA_RECOVERY_R2_ENABLED\n')
                if scenario=='unknown-existing':(base/'common.sh').write_text('unknown')
                if scenario=='symlink':(base/'enabled').unlink();(base/'enabled').symlink_to(source/'common.sh')
            if scenario=='unmounted':mounts.write_text('')
            if scenario=='wrong-camera':vendor.write_text('#!/bin/sh\necho NATIVE_CHANGED\n')
            if scenario=='corrupt-source':(source/'manage.sh').write_text('bad')
            if scenario=='startup-changed':init.write_text('changed')
            script=(BUILDER/'components/camera/firmware-launcher.sh').read_text()
            script=script.replace(camera.CAMERA_SHA,vendor_sha).replace(camera.INIT_SHA,sha(original))
            script=re.sub(r'/(?:opt|etc|tmp)/',lambda m:str(root)+m[0],script)
            script=script.replace('/proc/mounts',str(mounts)).replace('logger -t cc2-camera','echo')
            path=root/'boot.sh';path.write_text(script)
            env=dict(os.environ,PATH=str(root/'bin')+os.pathsep+os.environ['PATH']);env.pop('LD_PRELOAD',None)
            shell=shlex.split(os.environ.get('CC2_TEST_SHELL','sh'))
            result=subprocess.run([*shell,str(path),'original-arg'],env=env,text=True,capture_output=True)
            self.assertEqual(result.returncode,0,result.stderr)
            patched=scenario in ('fresh','legacy-enabled')
            self.assertIn('CAMERA:1:1:original-arg' if patched else ('NATIVE_CHANGED' if scenario=='wrong-camera' else 'CAMERA:0:0:original-arg'),result.stdout)
            if patched:
                self.assertTrue((base/'enabled').is_file())
                second=subprocess.run([*shell,str(path),'again'],env=env,text=True,capture_output=True)
                self.assertEqual(second.returncode,0,second.stderr);self.assertIn('CAMERA:1:1:again',second.stdout)
                self.assertEqual(len(list(base.parent.glob('camera-before-firmware-*'))),1 if scenario=='legacy-enabled' else 0)
                rolled=subprocess.run([*shell,str(base/'manage.sh'),'rollback'],env=env,text=True,capture_output=True)
                self.assertEqual(rolled.returncode,0,rolled.stderr);self.assertEqual(init.read_bytes(),original)
            if scenario in ('disabled','legacy-disabled'):self.assertFalse((base/'enabled').exists())
            if scenario=='unknown-existing':self.assertEqual((base/'common.sh').read_text(),'unknown')

    def test_fresh_seed_repeat_boot_and_exact_rollback(self):self.boot('fresh')
    def test_qualified_previous_installation_migrates(self):self.boot('legacy-enabled')
    def test_disabled_previous_installation_stays_disabled(self):self.boot('legacy-disabled')
    def test_disabled_current_installation_stays_disabled(self):self.boot('disabled')
    def test_unknown_previous_installation_is_preserved(self):self.boot('unknown-existing')
    def test_symlinked_enabled_marker_refused(self):self.boot('symlink')
    def test_unmounted_storage_starts_native(self):self.boot('unmounted')
    def test_changed_camera_starts_native(self):self.boot('wrong-camera')
    def test_corrupt_source_starts_native(self):self.boot('corrupt-source')
    def test_changed_startup_starts_native(self):self.boot('startup-changed')
    def test_failed_native_preflight_starts_native(self):self.boot('runtime-fails')

if __name__=='__main__':unittest.main()
