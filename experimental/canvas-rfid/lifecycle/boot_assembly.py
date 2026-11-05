"""Offline concrete boot assembly for the closed maintainer migration profile.

Builds bytes only. No filesystem writes, native execution or installation
authorization here. A trusted packaging caller verifies input build receipts.
"""
from pathlib import Path
from installed_profiles import Snapshot, TARGETS, UnsupportedProfile
from migration_plan import ROOT_GATE, compile_plan, sha

HERE=Path(__file__).resolve().parent
BASE='/opt/usr/cc2-rfid'
NATIVE='/opt/usr/cc2-reactor-runtime-v2/elegoo_printer'
REACTOR='/opt/usr/cc2-reactor-runtime-v2/libcc2-reactor-v2.so'
CAVITY='/opt/usr/cc2-cavity-floor/libcc2-cavity-floor-r1.so'
NETWORK='/opt/usr/cc2-network-manager/libcc2-network-compat-r1.so'
CONTROL='/opt/usr/cc2-network-manager/control'
CHECKER='/opt/usr/cc2-control/camera/cc2-camera-reap-check'
PINS={
    NATIVE:'19712b5943b10bb8cb6aab9a4ed498ee962b28538b23c91b7ed003eb9e590745',
    REACTOR:'4639364c68489ec40c4e0c4f425550f3aa19e44c2ee61ed641affdc560e7e92c',
    CAVITY:'52d400605dd51f89eb95248c42a1661b3c0e60fadec5453d27a987729332fb10',
    NETWORK:'bde512195dd6bf3846f3103b5e1acbd89a62e5ee819e405389c97f5fc188ff3a',
    CONTROL:'2a4dd2dd8e23aa3446348973ba6b8fa90a1fda16fcd96626015f8f0b816e9d62',
    CHECKER:'dce1180e3c04512fea1c4f9868b131f3fbe9bda187ebc9342f9db633f0a57ea0',
    '/opt/lib/libcolib.so':'14288a4a73c339b039dd89597618ebb05702f90d668b76f74019d5d15ce67403',
    '/opt/usr/cc2-cavity-floor/enabled':'bd925b9b6b76c1f0a513124be2b9310a217da065764bd184da874d4cfdccdd00',
}

def replace_once(data,before,after):
    if data.count(before)!=1:raise UnsupportedProfile('boot_template_or_profile_changed')
    return data.replace(before,after,1)

def configure(name,changes):
    data=(HERE/name).read_bytes()
    for before,after in changes.items():data=replace_once(data,before.encode(),after.encode())
    if b'UNCONFIGURED' in data:raise UnsupportedProfile('unconfigured_boot_template')
    return data

def activation(old,guard_sha,stock=False):
    # This boundary is an exact recognized old source, not a fuzzy patch of an
    # unknown activation program. Preserve camera/network policy and watchdogs.
    new=f'''    RFID={BASE}/libcc2-rfid.so
    case "$p" in
        *":$RFID")
            [ -f {BASE}/enabled ] && [ ! -L {BASE}/enabled ] || fail rfid_marker
            [ "$(cat {BASE}/enabled)" = CC2-RFID-NATIVE-R2 ] || fail rfid_marker
            [ -f "$RFID" ] && [ ! -L "$RFID" ] || fail rfid_library
            [ "$(sha256sum "$RFID" | awk '{{print $1}}')" = {guard_sha} ] || fail rfid_library
            grep -F "$RFID" "/proc/$printer/maps" >/dev/null || fail rfid_mapping
            p=${{p%:"$RFID"}}
            ;;
        *) fail rfid_guard_missing;;
    esac
'''.encode()
    if stock:
        # The caller recognizes the entire stock activation hash first. Strip
        # only the mandatory guard suffix for the unchanged reactor/shim checks.
        needle=b'    case "$p" in "$REACTOR"|"$OLD:$REACTOR"|"$LIB:$REACTOR") ;;*) fail printer_preload;;esac\n'
        result=replace_once(old,needle,new+needle)
        # Reuse already loaded network compatibility. A native restart must go
        # through the guarded full boot route, including a repair after failure.
        result=replace_once(result,b"    if ready;then echo 'activation:already_ready';exit 0;fi\n",
            b"    if ready;then echo 'activation:already_ready';exit 0;fi\n    fail rfid_guard_requires_full_idle_reboot\n")
        result=replace_once(result,b'    [ ! -f "$SNAP/activation.complete" ] || exit 0\n',
            b'    [ ! -f "$SNAP/activation.complete" ] || exit 0\n    fail rfid_guard_requires_full_idle_reboot\n')
        return result
    start=old.find(b'    RFID=/opt/usr/cc2-rfid-frame-fix-r1/')
    end=old.find(b'    CAVITY=',start)
    if start<0 or end<0:raise UnsupportedProfile('unknown_network_rfid_qualification')
    result=old[:start]+new+old[end:]
    before=b'[ ! -e /opt/usr/cc2-rfid-frame-fix-r1/enabled ] && [ ! -L /opt/usr/cc2-rfid-frame-fix-r1/enabled ] || fail rfid_trial_requires_full_idle_reboot'
    if result.count(before)!=2:raise UnsupportedProfile('unknown_network_restart_barrier')
    result=result.replace(before,b'fail rfid_guard_requires_full_idle_reboot')
    if b'/opt/usr/cc2-rfid-frame-fix-r1' in result:raise UnsupportedProfile('legacy_activation_dependency_retained')
    return result

def assemble(report,content,current_route,artifacts,catalogue=None):
    if set(artifacts)!={'startup-worker','recover-worker','libcc2-rfid.so'}:
        raise UnsupportedProfile('incomplete_boot_artifacts')
    # ELF class/endian/machine only; packaging must also validate the complete
    # corresponding test/build receipt and the actual artifact SHA.
    for name,data in artifacts.items():
        if len(data)<20 or data[:6]!=b'\x7fELF\x01\x01' or data[18:20]!=b'\x28\0':
            raise UnsupportedProfile('non_ARM_boot_artifact:'+name)
    component=dict(artifacts)
    component['fresh-guard.sh']=(HERE/'fresh-guard.sh').read_bytes()
    stock=bool(catalogue and catalogue.get('stock_bootstrap'))
    pins={path:digest for path,digest in PINS.items() if not stock or (path!=CAVITY and path!='/opt/usr/cc2-cavity-floor/enabled')}
    pins[BASE+'/libcc2-rfid.so']=sha(artifacts['libcc2-rfid.so'])
    component['guarded.sha256']=''.join(f'{digest}  {path}\n' for path,digest in sorted(pins.items())).encode()
    component['guarded-entry.sh']=configure('guarded-entry.sh',{
        'MANIFEST_SHA=UNCONFIGURED':'MANIFEST_SHA='+sha(component['guarded.sha256']),
        'CORE_PRELOADS='+REACTOR+':'+BASE+'/libcc2-rfid.so':
            'CORE_PRELOADS='+REACTOR+('' if stock else ':'+CAVITY)+':'+BASE+'/libcc2-rfid.so',
        'NETWORK_CONTROL=\n':'NETWORK_CONTROL='+CONTROL+'\n',
        'NETWORK_LIBRARY=\n':'NETWORK_LIBRARY='+NETWORK+'\n',
    })
    component['boot-dispatch.sh']=configure('boot-dispatch.sh',{
        'WORKER_SHA=UNCONFIGURED':'WORKER_SHA='+sha(artifacts['recover-worker']),
    })
    component['stock-entry.sh']=b'#!/bin/sh\nset -eu\nexec /opt/usr/cc2-rfid/guarded-entry.sh "$@"\n'
    component['enabled']=b'CC2-RFID-NATIVE-R2\n'
    root_gate=configure('startup-gate.sh',{'WORKER_SHA=UNCONFIGURED':'WORKER_SHA='+sha(artifacts['startup-worker'])})
    delegate=b'#!/bin/sh\nset -eu\nexec /etc/init.d/cc2-reactor-launch "$@"\n'
    view=Snapshot(report,content)
    updated_activation=activation(view.regular(TARGETS['network-activation']),sha(artifacts['libcc2-rfid.so']),stock)
    plan=compile_plan(report,content,current_route,{
        ROOT_GATE:root_gate,TARGETS['reactor-launcher']:delegate,
        TARGETS['network-entry']:delegate,TARGETS['network-activation']:updated_activation,
    },catalogue)
    records=[]
    for index,target in enumerate(plan['targets']):
        component[f'payload/{index}']=target['payload']
        records.append(f'{index} {target["before_mode"]:04o} {target["after_mode"]:04o} {target["before_sha256"]} {target["after_sha256"]}\n')
    component['startup-plan']=(b'CC2-STARTUP-PLAN-1\n'+
        f'dispatch {sha(component["boot-dispatch.sh"])}\nstock-entry {sha(component["stock-entry.sh"])}\n'.encode()+''.join(records).encode())
    roles=[('bootstrap',BASE+'/startup-plan',sha(component['startup-plan'])),
           ('native',NATIVE,PINS[NATIVE]),('guard',BASE+'/libcc2-rfid.so',sha(component['libcc2-rfid.so'])),
           ('entry',BASE+'/guarded-entry.sh',sha(component['guarded-entry.sh'])),
           ('recover',BASE+'/recover-worker',sha(component['recover-worker'])),
           ('marker',BASE+'/enabled',sha(component['enabled']))]
    component['recovery-profile']=''.join(f'{role} {digest} {path}\n' for role,path,digest in roles).encode()
    modes={name:(0o700 if name.endswith('.sh') or name in ['startup-worker','recover-worker'] else 0o600) for name in component}
    return {'component_files':component,'component_modes':modes,'startup_plan':plan,
            'recovery_profile_sha256':sha(component['recovery-profile']),
            'installation_authorized':False,'live_authorization_required':True,
            'initial_lifecycle_phase':'HOST_STAGED','physical_flash_readback_available':False,
            'stock_bootstrap':stock,'requires_cavity':not stock}
