"""Committed builder inputs must match the SHA-256 values pinned in firmware_builder.py.

Objective: catch a byte-level drift (for example a line-ending normalisation that
drops a trailing CR) as soon as it is committed. Without this check it only
surfaces at preflight, and only for someone who has the restricted inputs needed
to run a full build; the other builder tests skip before reaching these files.
"""
from pathlib import Path
import hashlib
import importlib.util

BASE=Path(__file__).resolve().parents[1]
spec=importlib.util.spec_from_file_location('builder_pins',BASE/'core/firmware_builder.py')
b=importlib.util.module_from_spec(spec);spec.loader.exec_module(b)

pinned={
    'components/ssh/sshd.init':b.EXPECTED['sshd_init'],
    'components/ssh/sshd_config':b.EXPECTED['sshd_config'],
    'patches/z-offset/patch_zoffset.py':b.EXPECTED['zoffset_patcher'],
    'dualtrust/apply_dualtrust.py':b.EXPECTED['dual_patcher'],
    'tools/cc2_sig_tool_v1.1.py':b.EXPECTED['sig_tool'],
    'patches/http-upload/http_upload_v1.json':b.HTTP_UPLOAD_MANIFEST_SHA256,
    'keys/cc2_stock_public.pem':b.PUBLIC_HASHES['stock'],
    'dualtrust/cc2_community_release_public.pem':b.PUBLIC_HASHES['community'],
}
for relative,expected in pinned.items():
    actual=hashlib.sha256((BASE/relative).read_bytes()).hexdigest()
    assert actual==expected,f'{relative}: SHA-256 {actual} does not match pinned {expected}'

print(f'PASS: {len(pinned)} committed builder inputs match their pinned SHA-256 values.')
