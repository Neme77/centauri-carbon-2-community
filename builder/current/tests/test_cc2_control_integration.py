from pathlib import Path
import hashlib
import importlib.util
import json
import tempfile
import unittest

BASE=Path(__file__).resolve().parents[1]
spec=importlib.util.spec_from_file_location('builder42',BASE/'core/firmware_builder.py')
b=importlib.util.module_from_spec(spec);spec.loader.exec_module(b)
source=BASE/'components/cc2-control/source/source.zip'
if not source.is_file():
    raise unittest.SkipTest('release source archive is an external build input')
assert hashlib.sha256(source.read_bytes()).hexdigest()==b.CC2_CONTROL_SOURCE_SHA256

runtime_start=(BASE/'components/cc2-control/runtime/start.sh').read_text(encoding='utf-8')
runtime_init=(BASE/'components/cc2-control/runtime/cc2-control.init').read_text(encoding='utf-8')
mount_guard="while ! grep -q ' /opt/usr ' /proc/mounts; do"
assert mount_guard in runtime_start
assert runtime_start.index(mount_guard) < runtime_start.index('mkdir -p "$PERSIST"')
assert 'chmod 755 "$PERSIST"' in runtime_start
assert '/opt/inst/cc2-control/start.sh' in runtime_init
assert '/opt/usr/cc2-control/launch.sh' not in runtime_init

with tempfile.TemporaryDirectory() as temporary:
    root=Path(temporary)
    component=root/'prepared';component.mkdir()
    files={}
    for relative in b.CC2_CONTROL_FILES:
        path=component/relative;path.parent.mkdir(parents=True,exist_ok=True)
        path.write_bytes(('TEST '+relative+'\n').encode())
        path.chmod(0o644 if relative in ('web/index.html','defaults/material-presets.json') else 0o755)
        files[relative]={'sha256':hashlib.sha256(path.read_bytes()).hexdigest(),
                         'mode':oct(path.stat().st_mode&0o777)}
    manifest_path=root/'manifest.json'
    manifest_path.write_text(json.dumps({'component':'CC2 Control','version':'1.1.30',
        'source_sha256':b.CC2_CONTROL_SOURCE_SHA256,'files':files}))
    prepared,manifest=b.load_cc2_control(component,manifest_path)
    image=root/'rootfs';image.mkdir()
    b.install_cc2_control(image,prepared,manifest)
    b.audit_cc2_control(image,prepared,manifest)
    assert not (image/'opt/usr/cc2-control').exists(), 'persistent state must not be embedded in rootfs'
    assert (image/'etc/rc.d/S98cc2-control').readlink()==Path('../init.d/cc2-control')
    assert (image/'etc/rc.d/K10cc2-control').readlink()==Path('../init.d/cc2-control')

print('PASS: CC2 Control files, permissions, init links and persistent-state separation.')
