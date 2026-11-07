"""Admit only the source-reviewed, physically accepted native release bytes.

This build-time gate does not replace device-side profile, journal or safety
checks. It authorizes neither an arbitrary same-version binary nor new firmware.
"""
import hashlib
import io
import json
from pathlib import Path
import tarfile

from app_binding_bundle import read_runtime, unique_pairs


def validate_release(runtime, binary, version, policy_path=None):
    policy_path=Path(policy_path) if policy_path else Path(__file__).with_name('native-release.json')
    policy=json.loads(policy_path.read_text(),object_pairs_hook=unique_pairs)
    data=runtime.read_bytes()
    current_version=Path(__file__).resolve().parents[1].joinpath('VERSION').read_text().strip()
    if (type(policy.get('protocol')) is not int or policy['protocol']!=1 or
            version!=current_version or policy.get('version_sha256')!=hashlib.sha256(current_version.encode()).hexdigest() or policy.get('community_base')!='4.4.3' or
            policy.get('physical_flash_readback') is not False or
            hashlib.sha256(binary).hexdigest()!=policy.get('app_sha256') or
            hashlib.sha256(data).hexdigest()!=policy.get('native_runtime_sha256')):
        raise ValueError('Native release differs from the accepted application/runtime qualification')
    # Fixed accepted size/hash above; the ordinary packager independently
    # validates all members, nested manifests and first-install/update parity.
    with tarfile.open(fileobj=io.BytesIO(data),mode='r:gz') as archive:
        entries=archive.getmembers()
        if {m.name for m in entries}!={'delivery.json','rfid.tar.gz','cavity.tar.gz','binding.tar.gz'} or len(entries)!=4:
            raise ValueError('Incomplete qualified native release')
        rfid=archive.extractfile('rfid.tar.gz').read()
    class Blob:
        def read_bytes(self):return rfid
    _,_,metadata=read_runtime(Blob())
    if metadata['app_sha256']!=policy['app_sha256'] or metadata['profile_sha256']!=policy.get('recovery_profile_sha256'):
        raise ValueError('Qualified native release profile mismatch')
    return policy
