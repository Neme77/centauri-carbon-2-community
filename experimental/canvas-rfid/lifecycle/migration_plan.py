"""Compile the fixed eight-file migration plan from a recognized snapshot.

Offline only. This supplies transaction payloads, not live authorization or an
installer. Replacement entry programs must come from the qualified builder,
never from HTTP input. Native binary and MCU changes are outside this module.
"""
import hashlib
import re
from installed_profiles import Snapshot, TARGETS, UnsupportedProfile, analyze_snapshot

ROOT_GATE='/etc/init.d/cc2-reactor-launch'
STARTUP_MANIFEST='/opt/usr/cc2-network-manager/startup.sha256'
RUN_PRINTER='/opt/bin/run_printer.sh'
RUN_SHA='05e4614779fc59b43c604eda7d85f1e8534b24d3cf284b16226c9c3bbc975118'

def sha(data):return hashlib.sha256(data).hexdigest()

def update_manifest(data,changes):
    """Update one exact path per change; preserve all unrelated bytes/order."""
    result=[];seen=set();done=set()
    for line in data.decode('ascii').splitlines(keepends=True):
        match=re.fullmatch(r'([0-9a-f]{64})(  | \*)([^\r\n]+)\n',line)
        if not match:raise UnsupportedProfile('invalid_manifest_line')
        name=match[3]
        canonical=name[2:] if name.startswith('./') else name
        if canonical in seen:raise UnsupportedProfile('duplicate_manifest_path')
        seen.add(canonical)
        if canonical in changes:
            old,new=changes[canonical]
            if match[1]!=old:raise UnsupportedProfile('manifest_old_hash_changed:'+canonical)
            line=new+match[2]+name+'\n';done.add(canonical)
        result.append(line)
    if done!=set(changes):raise UnsupportedProfile('manifest_target_missing')
    return ''.join(result).encode('ascii')

def compile_plan(report,content,current_run_printer,replacements,catalogue=None):
    recognized=analyze_snapshot(report,content,catalogue)
    if sha(current_run_printer)!=RUN_SHA:raise UnsupportedProfile('current_startup_route_changed')
    paths={ROOT_GATE,TARGETS['reactor-launcher'],TARGETS['network-entry'],TARGETS['network-activation']}
    if set(replacements)!=paths:raise UnsupportedProfile('incomplete_or_extra_entry_replacements')
    for path,data in replacements.items():
        if not isinstance(data,bytes) or not data.startswith(b'#!/bin/sh\n') or b'\0' in data or len(data)>65536:
            raise UnsupportedProfile('invalid_generated_entry:'+path)
        if b'UNCONFIGURED' in data:raise UnsupportedProfile('unconfigured_generated_entry:'+path)
    view=Snapshot(report,content)
    pending=dict(replacements)
    launcher=TARGETS['reactor-launcher'];profile=TARGETS['network-profile']
    old_launcher=view.files[launcher]['sha256']
    new_launcher=sha(pending[launcher])
    # Preserve the original qualification logic, replacing the single exact
    # launcher identity only. Other component checks remain byte-for-byte.
    original=view.regular(profile)
    needle=('[ "$(sha '+launcher+')" = '+old_launcher+' ] || fail reactor_launcher_profile').encode()
    if original.count(needle)!=1:raise UnsupportedProfile('network_launcher_check_changed')
    pending[profile]=original.replace(needle,needle.replace(old_launcher.encode(),new_launcher.encode()))
    reactor_manifest=TARGETS['reactor-manifest']
    pending[reactor_manifest]=update_manifest(view.regular(reactor_manifest),{'launcher.sh':(old_launcher,new_launcher)})
    network_manifest=TARGETS['network-manifest']
    pending[STARTUP_MANIFEST]=update_manifest(view.regular(STARTUP_MANIFEST),{
        ROOT_GATE:(view.files[ROOT_GATE]['sha256'],sha(pending[ROOT_GATE]))})
    changes={name:(view.files[path]['sha256'],sha(pending[path])) for name,path in [
        ('profile.sh',profile),('reactor-entry.sh',TARGETS['network-entry']),('activate.sh',TARGETS['network-activation'])]}
    # Firmware-seeded Connections includes the startup manifest in its own
    # manifest; older PC installations do not. Update this dependency before
    # hashing the parent manifest, preserving the exact original path spelling.
    original_manifest=view.regular(network_manifest)
    startup_rows=[line for line in original_manifest.decode('ascii').splitlines()
                  if line.split('  ',1)[-1].removeprefix('./')=='startup.sha256']
    if startup_rows:
        changes['startup.sha256']=(view.files[STARTUP_MANIFEST]['sha256'],sha(pending[STARTUP_MANIFEST]))
    elif catalogue and catalogue.get('stock_bootstrap'):
        raise UnsupportedProfile('firmware_startup_manifest_dependency_missing')
    pending[network_manifest]=update_manifest(original_manifest,changes)
    targets=[];plan_lines=[]
    for index,old in enumerate(recognized['startup_targets']):
        path=old['path'];data=pending[path];mode=old['before_mode']
        if mode not in (0o600,0o644,0o700,0o755):raise UnsupportedProfile('unsupported_target_mode')
        item={**old,'after_sha256':sha(data),'after_mode':mode,'size':len(data),'payload':data}
        targets.append(item)
        plan_lines.append(f'{index} {mode:04o} {mode:04o} {old["before_sha256"]} {sha(data)} {path}\n')
    if len(targets)!=8 or set(pending)!={x['path'] for x in targets}:raise UnsupportedProfile('wrong_target_set')
    return {'recognized':recognized,'startup_plan_sha256':sha(''.join(plan_lines).encode()),
            'targets':targets,'current_run_printer_sha256':RUN_SHA,
            'offline_only':True,'installation_authorized':False}
