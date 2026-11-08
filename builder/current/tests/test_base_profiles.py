"""The vendor base is selected in one place and unported bases are refused.

Objective: BASE_FIRMWARE names a base profile; the qualified 02.01.01.07 profile
must reproduce the historical pins exactly, and a profile that still contains
TODO_ placeholders or lacks a ported patch set must be refused before any
firmware is read.
"""
from pathlib import Path
import importlib.util
import json
import subprocess
import sys
import tempfile

BASE = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location('builder_profiles', BASE / 'core/firmware_builder.py')
b = importlib.util.module_from_spec(spec)
spec.loader.exec_module(b)

assert b.selected_base_version() == (BASE / 'BASE_FIRMWARE').read_text().strip()
q = b.load_base_profile(b.QUALIFIED_BASE)
assert b.require_buildable_profile(q) is q
for key in ('stock_outer', 'stock_inner', 'stock_swu', 'sw-description', 'resource', 'uboot',
            'boot0', 'kernel', 'stock_rootfs', 'stock_gui', 'daemon_stock'):
    assert b.EXPECTED[key] == q['sha256'][key], key
assert q['manifest'] == {'version': '02.01.01.07', 'update_class': '00.00.00.00'}

for path in sorted((BASE / 'base_profiles').glob('*.json')):
    profile = b.load_base_profile(path.stem)
    if path.stem == b.QUALIFIED_BASE:
        continue
    try:
        b.require_buildable_profile(profile)
    except RuntimeError as error:
        assert 'refusing to build' in str(error)
    else:
        raise AssertionError(f'unported base profile {path.stem} accepted')

candidate = b.load_base_profile('02.01.01.13')
open_items = b.profile_placeholders(candidate)
# Measured from the captured official package; only the AES-protected manifest fields stay open.
assert open_items == ['manifest.version = TODO_MANIFEST_VERSION', 'manifest.update_class = TODO_MANIFEST_UPDATE_CLASS'], open_items
assert candidate['ported_patch_set'] is False and candidate['stock_package'] == 'cc2_eeb001_02.01.01.13_20261020214142.zip.sig'
for key in ('uboot', 'boot0', 'sw-description', 'resource', 'daemon_stock', 'libcolib'):
    assert candidate['sha256'][key] == q['sha256'][key], key
for key in ('kernel', 'stock_rootfs', 'stock_gui', 'stock_printer', 'stock_ai_camera'):
    assert candidate['sha256'][key] != q['sha256'][key], key

with tempfile.TemporaryDirectory() as td:
    tampered = Path(td)
    data = json.loads((BASE / 'base_profiles/02.01.01.07.json').read_text())
    data['sha256']['stock_gui'] = '0' * 64
    (tampered / '02.01.01.07.json').write_text(json.dumps(data))
    try:
        b.load_base_profile('02.01.01.07', tampered)
    except RuntimeError as error:
        assert 'SHA256 mismatch' in str(error)
    else:
        raise AssertionError('tampered qualified profile accepted')
    complete = json.loads((BASE / 'base_profiles/02.01.01.13.json').read_text())
    complete['manifest'] = {'version': '02.01.01.13', 'update_class': '00.00.00.00'}
    try:
        b.require_buildable_profile(complete)
    except RuntimeError as error:
        assert 'No ported and qualified patch set' in str(error)
    else:
        raise AssertionError('unported base profile accepted')
    complete['ported_patch_set'] = True
    try:
        b.require_buildable_profile(complete)
    except RuntimeError as error:
        assert 'No ported and qualified patch set' in str(error)
    else:
        raise AssertionError('base without a patch implementation accepted')
    complete = json.loads((BASE / 'base_profiles/02.01.01.13.json').read_text())
    complete['ported_patch_set'] = True
    try:
        b.require_buildable_profile(complete)
    except RuntimeError as error:
        assert 'open placeholder' in str(error)
    else:
        raise AssertionError('placeholder profile accepted')

# The CLI refuses an unported base before reading keys, firmware or tools.
r = subprocess.run([sys.executable, str(BASE / 'core/firmware_builder.py'), '--base-version', '02.01.01.13',
                    '--private-key', str(BASE / 'keys/cc2_stock_public.pem')], capture_output=True, text=True)
assert r.returncode == 1 and 'open placeholder' in r.stderr and 'signing' not in r.stderr.lower(), r.stderr
print(f'PASS: base profiles; qualified pins unchanged; {len(open_items)} placeholders refused for 02.01.01.13.')
