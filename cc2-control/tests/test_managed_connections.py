"""Preserve an authenticated composed network profile, never overwrite it."""
from pathlib import Path
import hashlib
import os
import subprocess
import sys
import tempfile

sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'installer'))
from network_component import preserve_managed

def digest(data):return hashlib.sha256(data).hexdigest()

with tempfile.TemporaryDirectory(prefix='cc2-managed-connections-') as temporary:
    base=Path(temporary);count=0
    for shell in [['dash'],['busybox','sh']]:
        for route in ['fresh','without-canvas','upgrade','current']:
            for case in ['good','bad-manifest','bad-file','bad-platform','manifest-link','manager-link','missing','unverified-base']:
                if case=='unverified-base' and route not in ['fresh','without-canvas']:continue
                root=base/(shell[0]+'-'+route+'-'+case);root.mkdir()
                manager=root/'manager';manager.mkdir();(manager/'control').write_bytes(b'qualified control')
                before=(digest(b'qualified control')+'  control\n').encode()
                after=(digest(b'qualified control')+'  ./control\n').encode()
                (manager/'SHA256SUMS').write_bytes(before if route in ['fresh','without-canvas'] else after)
                if case=='bad-manifest':(manager/'SHA256SUMS').write_bytes(b'foreign manifest')
                if case=='bad-file':(manager/'control').write_bytes(b'changed control')
                if case=='manifest-link':
                    data=(manager/'SHA256SUMS').read_bytes();(manager/'SHA256SUMS').unlink()
                    (root/'manifest').write_bytes(data);(manager/'SHA256SUMS').symlink_to(root/'manifest')
                if case=='manager-link':
                    manager.rename(root/'real-manager');manager.symlink_to(root/'real-manager',target_is_directory=True)
                if case=='missing':
                    (manager/'control').unlink();(manager/'SHA256SUMS').unlink();manager.rmdir()
                code=preserve_managed(digest(before),digest(after)).replace('/opt/usr/cc2-network-manager',str(manager))
                script=root/'run.sh'
                script.write_text('set -eu\nNATIVE_ROUTE='+route+'\nNATIVE_NETWORK_BASE_VERIFIED='+('0' if case=='unverified-base' else '1')+'\nnetwork_platform_supported(){ return '+('1' if case=='bad-platform' else '0')+'; }\nnetwork_install_component(){ echo LEGACY_INSTALL; }\n'+code+'\nnative_connections_preflight\nnative_connections_component\n')
                subprocess.run([*shell,'-n',str(script)],check=True,timeout=10)
                result=subprocess.run([*shell,str(script)],capture_output=True,text=True,timeout=10)
                assert (result.returncode==0)==(case in ['good','missing']),(shell,route,case,result.stdout,result.stderr)
                assert ('LEGACY_INSTALL' in result.stdout)==(case=='missing')
                if case=='good':
                    assert 'CONNECTIONS_COMPONENT_PRESERVED' in result.stdout
                    assert (manager/'control').read_bytes()==b'qualified control'
                    assert (manager/'SHA256SUMS').read_bytes()==(before if route in ['fresh','without-canvas'] else after)
                count+=1
    for value in ['', '../escape','ab'*31,'A'*64]:
        try:preserve_managed(value,'ab'*32)
        except ValueError:pass
        else:raise AssertionError('Invalid binding accepted')
print(f'PASS: {count} fresh/without-Canvas/upgrade/current managed Connections dash/BusyBox cases, exact manifests/files and platform guard, no overwrite, wrong data/symlinks refused')
