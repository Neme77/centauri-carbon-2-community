#!/usr/bin/env python3
"""Stage only the previously qualified camera libraries supplied with this builder."""
from pathlib import Path
import hashlib
import importlib.util
import json
import shutil
import struct

HERE=Path(__file__).resolve().parent
spec=importlib.util.spec_from_file_location('camera_firmware',HERE/'core/camera_component.py')
camera=importlib.util.module_from_spec(spec);spec.loader.exec_module(camera)

def main():
    component=HERE/'components/camera';prepared=component/'prepared'
    runtime=component/'runtime'
    for name,expected in camera.payload.PINS.items():
        path=runtime/name
        if camera.sha(path)!=expected:raise RuntimeError('Missing or unqualified camera runtime: '+name)
        data=path.read_bytes()
        if data[:6]!=b'\x7fELF\x01\x01' or struct.unpack_from('<H',data,18)[0]!=40:
            raise RuntimeError('Camera runtime is not ARM32: '+name)
    prepared.mkdir(parents=True,exist_ok=True)
    for name in ('common.sh','manage.sh','boot-camera.sh'):
        shutil.copyfile(camera.REPO/'cc2-control/installer/camera'/name,prepared/name)
    for name in camera.payload.PINS:shutil.copyfile(runtime/name,prepared/name)
    shutil.copyfile(component/'firmware-launcher.sh',prepared/'firmware-launcher.sh')
    sums=''.join(camera.sha(prepared/name)+'  '+name+'\n' for name in sorted(camera.FILES) if name!='SHA256SUMS')
    (prepared/'SHA256SUMS').write_text(sums)
    files={name:camera.sha(prepared/name) for name in (*camera.FILES,'firmware-launcher.sh')}
    (component/'prepared-manifest.json').write_text(json.dumps({'camera_sha256':camera.CAMERA_SHA,'files':files},indent=2)+'\n')
    camera.load(component)
    print('CAMERA PREPARED: unchanged qualified R2 libraries; vendor camera supplied only by the local official firmware input.')

if __name__=='__main__':main()
