"""Install the qualified R2 camera runtime without embedding vendor binaries."""
from pathlib import Path
import hashlib
import importlib.util
import json
import re
import shutil

BUILDER=Path(__file__).resolve().parents[1]
REPO=BUILDER.parents[1]
spec=importlib.util.spec_from_file_location('camera_payload',REPO/'cc2-control/installer/camera_component.py')
payload=importlib.util.module_from_spec(spec);spec.loader.exec_module(payload)
CAMERA_SHA='ec5633b17de329d5b18c3f8d8de6ebf389391a04193536d976ba167022d07b99'
INIT_SHA='bcabb1fa405b066b06c229c6d8cc6e474a0ccc57e31a1b5b387d3b55945be563'
FILES=('common.sh','manage.sh','boot-camera.sh',*payload.PINS,'SHA256SUMS')

def sha(path):return hashlib.sha256(Path(path).read_bytes()).hexdigest()

def patched_init(text):
    result,count=re.subn(r'(?m)^([ \t]*)ai_camera &[ \t]*$',r'\1/etc/init.d/cc2-camera-launch & # CC2_CAMERA_FIRMWARE_R2',text)
    if count!=1 or 'CC2_CAMERA_' in text:raise RuntimeError('Expected one native camera launch')
    return result

def load(component):
    component=Path(component)
    manifest=json.loads((component/'prepared-manifest.json').read_text())
    expected=set(FILES)|{'firmware-launcher.sh'}
    if manifest.get('camera_sha256')!=CAMERA_SHA or set(manifest.get('files',{}))!=expected:
        raise RuntimeError('Invalid camera firmware component identity')
    for name,value in manifest['files'].items():
        if sha(component/'prepared'/name)!=value:raise RuntimeError('Camera component checksum mismatch: '+name)
    for name,value in payload.PINS.items():
        if manifest['files'][name]!=value:raise RuntimeError('Unqualified camera runtime: '+name)
    for name in ('common.sh','manage.sh','boot-camera.sh'):
        if sha(REPO/'cc2-control/installer/camera'/name)!=manifest['files'][name]:raise RuntimeError('Stale camera shell source')
    if sha(BUILDER/'components/camera/firmware-launcher.sh')!=manifest['files']['firmware-launcher.sh']:
        raise RuntimeError('Stale camera firmware launcher')
    sums=''.join(manifest['files'][n]+'  '+n+'\n' for n in sorted(FILES) if n!='SHA256SUMS')
    if (component/'prepared/SHA256SUMS').read_text()!=sums:raise RuntimeError('Camera manifest file set mismatch')
    return component,manifest

def install(root,component):
    root=Path(root);component,manifest=load(component)
    init=root/'etc/init.d/printer'
    if sha(root/'opt/bin/ai_camera')!=CAMERA_SHA or sha(init)!=INIT_SHA:
        raise RuntimeError('Camera integration requires the exact qualified camera and reactor-coordinated startup')
    original=init.read_bytes();patched=patched_init(original.decode()).encode()
    destination=root/'opt/inst/cc2-camera-r2';destination.mkdir(parents=True,exist_ok=False)
    for name in FILES:
        shutil.copyfile(component/'prepared'/name,destination/name)
        (destination/name).chmod(0o700 if name.endswith('.sh') or name=='cc2-camera-reap-check' else 0o600)
    (destination/'printer.original').write_bytes(original)
    (destination/'printer.original').chmod(0o600)
    (destination/'patched.sha256').write_text(hashlib.sha256(patched).hexdigest()+'\n')
    (destination/'patched.sha256').chmod(0o600)
    hook=root/'etc/init.d/cc2-camera-launch'
    shutil.copyfile(component/'prepared/firmware-launcher.sh',hook);hook.chmod(0o755)
    init.write_bytes(patched)
    return manifest

def audit(root,component):
    root=Path(root);component,manifest=load(component);destination=root/'opt/inst/cc2-camera-r2'
    for name in FILES:
        if sha(destination/name)!=manifest['files'][name]:raise RuntimeError('Rebuilt camera payload mismatch: '+name)
        mode=0o700 if name.endswith('.sh') or name=='cc2-camera-reap-check' else 0o600
        if (destination/name).stat().st_mode&0o777!=mode:raise RuntimeError('Rebuilt camera mode mismatch: '+name)
    if sha(root/'opt/bin/ai_camera')!=CAMERA_SHA or sha(destination/'printer.original')!=INIT_SHA:
        raise RuntimeError('Rebuilt camera/base startup mismatch')
    init=root/'etc/init.d/printer'
    if init.read_text()!=patched_init((destination/'printer.original').read_text()) or sha(init)!=(destination/'patched.sha256').read_text().strip():
        raise RuntimeError('Rebuilt camera launch coordination mismatch')
    hook=root/'etc/init.d/cc2-camera-launch'
    if sha(hook)!=manifest['files']['firmware-launcher.sh'] or hook.stat().st_mode&0o777!=0o755:
        raise RuntimeError('Rebuilt camera launch hook mismatch')
