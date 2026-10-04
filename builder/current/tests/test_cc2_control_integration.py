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

def prepare(root,extra=()):
    component=root/'prepared';component.mkdir()
    files={}
    for relative in b.CC2_CONTROL_FILES+tuple(extra):
        path=component/relative;path.parent.mkdir(parents=True,exist_ok=True)
        path.write_bytes(('TEST '+relative+'\n').encode())
        path.chmod(0o755 if relative in ('cc2-control','start.sh','launch.sh','cc2-control.init','cc2-configure') else 0o644)
        files[relative]={'sha256':hashlib.sha256(path.read_bytes()).hexdigest(),
                         'mode':oct(path.stat().st_mode&0o777)}
    manifest_path=root/'manifest.json'
    manifest_path.write_text(json.dumps({'component':'CC2 Control','version':b.cc2_control_version(),
        'source_sha256':b.CC2_CONTROL_SOURCE_SHA256,'source_commit':b.CC2_CONTROL_SOURCE_COMMIT,'files':files}))
    return b.load_cc2_control(component,manifest_path)

def install(root,prepared,manifest):
    image=root/'rootfs';image.mkdir()
    b.install_cc2_control(image,prepared,manifest)
    b.audit_cc2_control(image,prepared,manifest)
    assert not (image/'opt/usr/cc2-control').exists(), 'persistent state must not be embedded in rootfs'
    assert (image/'etc/rc.d/S98cc2-control').readlink()==Path('../init.d/cc2-control')
    assert (image/'etc/rc.d/K10cc2-control').readlink()==Path('../init.d/cc2-control')
    return image

# UI snapshots that still embed their translations: no locale files at all.
with tempfile.TemporaryDirectory() as temporary:
    image=install(Path(temporary),*prepare(Path(temporary)))
    assert not (image/'opt/inst/cc2-control/web/locales').exists()

# JSON-backed UI: any number of web/locales/<code>.json files, installed as-is.
with tempfile.TemporaryDirectory() as temporary:
    locales=('web/locales/en.json','web/locales/fr.json','web/locales/pt-BR.json')
    image=install(Path(temporary),*prepare(Path(temporary),locales))
    for relative in locales:
        assert (image/'opt/inst/cc2-control'/relative).is_file(), relative
    assert (image/'opt/inst/cc2-control/web/locales/fr.json').stat().st_mode&0o777==0o644

# Fail closed on locale names outside the pattern or a missing source locale.
for bad in (('web/locales/en.json','web/locales/EN2.json'),
            ('web/locales/en.json','web/locales/../cc2-control.json'),
            ('web/locales/fr.json',),
            ('web/locales/en.json','web/extra.json')):
    with tempfile.TemporaryDirectory() as temporary:
        try:
            prepare(Path(temporary),bad)
        except RuntimeError:
            continue
        raise AssertionError(f'accepted invalid CC2 Control file set: {bad}')

# A prepared component from the earlier snapshot must fail before installation.
with tempfile.TemporaryDirectory() as temporary:
    root=Path(temporary)
    component,manifest=prepare(root)
    manifest['source_commit']='0'*40
    path=root/'manifest.json'
    path.write_text(json.dumps(manifest))
    try:
        b.load_cc2_control(component,path)
    except RuntimeError as error:
        assert 'source commit mismatch' in str(error)
    else:
        raise AssertionError('stale prepared component accepted')

# A manifest whose version differs from the pinned snapshot's VERSION is rejected.
with tempfile.TemporaryDirectory() as temporary:
    root=Path(temporary)
    component,manifest=prepare(root)
    manifest['version']='0.0.0'
    path=root/'manifest.json'
    path.write_text(json.dumps(manifest))
    try:
        b.load_cc2_control(component,path)
    except RuntimeError as error:
        assert 'identity mismatch' in str(error)
    else:
        raise AssertionError('manifest with another version accepted')

print('PASS: CC2 Control files, optional locales, permissions, init links and persistent-state separation.')
