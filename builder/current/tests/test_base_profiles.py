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
# The 07 base-specific release pins now live in the profile's port.outputs and
# must stay character-identical to the module constants (frozen copy, section 4).
assert q['port']['outputs']['printer_release']['sha256'] == b.PRINTER_RELEASE_SHA256
assert q['port']['outputs']['camera_release'] == b.CAMERA_RELEASE_SHA256

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
# Native display is declared not-included for 02.01.01.13 (components section),
# so there is no native_gui placeholder: 0113 has no open placeholders and is
# refused only by the qualified-base gate below.
assert open_items == [], open_items
# The validated surface excludes native display, with a documented reason; 07
# (no components section) includes it by default.
assert b.component_included(candidate, 'native_display') is False
assert candidate['components']['native_display']['reason'].strip()
assert b.component_included(q, 'native_display') is True
# A component excluded without a reason is refused (no silent omission, 3.5).
try:
    b.component_included({'version': 'x', 'components': {'native_display': {'included': False}}}, 'native_display')
except RuntimeError as error:
    assert 'reason' in str(error), error
else:
    raise AssertionError('excluded component without a reason accepted')
assert candidate['manifest'] == {'version': '02.01.01.13', 'update_class': '00.00.00.00'}
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
    # 0113 has no open placeholder (native display is declared not-included), so
    # require_buildable_profile reaches the qualified-base gate directly.
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
    complete['manifest']['version'] = 'TODO_MANIFEST_VERSION'
    try:
        b.require_buildable_profile(complete)
    except RuntimeError as error:
        assert 'open placeholder' in str(error)
    else:
        raise AssertionError('placeholder profile accepted')

# QUALIFIED_BASES is the real gate: a base qualifies only by being listed there
# (a reviewed code change), never by editing a profile file alone. 02.01.01.07
# is listed and maps to its own profile hash; 02.01.01.13 is not listed, so a
# complete, ported 02.01.01.13 profile is still refused.
assert b.QUALIFIED_BASE in b.QUALIFIED_BASES
assert b.QUALIFIED_BASES['02.01.01.07'] == b.QUALIFIED_PROFILE_SHA256
assert '02.01.01.13' not in b.QUALIFIED_BASES
saved = dict(b.QUALIFIED_BASES)
try:
    b.QUALIFIED_BASES['02.01.01.13'] = b.load_base_profile('02.01.01.13', None) and __import__('hashlib').sha256(
        (BASE / 'base_profiles/02.01.01.13.json').read_bytes()).hexdigest()
    ported = json.loads((BASE / 'base_profiles/02.01.01.13.json').read_text())
    ported['ported_patch_set'] = True
    assert b.require_buildable_profile(ported) is ported  # now accepted, proving the set is the gate
finally:
    b.QUALIFIED_BASES.clear()
    b.QUALIFIED_BASES.update(saved)
assert '02.01.01.13' not in b.QUALIFIED_BASES

# The CLI refuses an unported base before reading keys, firmware or tools.
r = subprocess.run([sys.executable, str(BASE / 'core/firmware_builder.py'), '--base-version', '02.01.01.13',
                    '--private-key', str(BASE / 'keys/cc2_stock_public.pem')], capture_output=True, text=True)
# 02.01.01.13 is refused before any signing: it is not a qualified base (native
# display is declared not-included, so there is no placeholder). The message
# contains 'refusing to build'.
assert r.returncode == 1 and 'refusing to build' in r.stderr and 'signing' not in r.stderr.lower(), r.stderr
print('PASS: base profiles; qualified pins unchanged; complete 02.01.01.13 profile refused until a patch set is ported.')
