"""Closed installed-profile recognition for migration planning only.

This does not authorize a native stop, startup write or Canvas transfer. The
installer must repeat live identity, freshness and cold/idle checks itself.
"""
import hashlib
import json
from pathlib import Path
import re
import stat
import struct

HERE=Path(__file__).resolve().parent
TARGETS={
    'reactor-launcher':'/opt/usr/cc2-reactor-runtime-v2/launcher.sh',
    'reactor-manifest':'/opt/usr/cc2-reactor-runtime-v2/SHA256SUMS',
    'network-profile':'/opt/usr/cc2-network-manager/profile.sh',
    'network-entry':'/opt/usr/cc2-network-manager/reactor-entry.sh',
    'network-activation':'/opt/usr/cc2-network-manager/activate.sh',
    'network-manifest':'/opt/usr/cc2-network-manager/SHA256SUMS',
}
STAGES=('cc2-rfid-native-r1','cc2-rfid-native-r2','cc2-rfid-frame-fix-r1')
class UnsupportedProfile(ValueError):pass

def _sha(data):return hashlib.sha256(data).hexdigest()

class Snapshot:
    def __init__(self,report,content):
        self.report=report;self.content=content;self.files={}
        for item in report['files']:
            if item['path'] in self.files:raise UnsupportedProfile('duplicate_file_record')
            self.files[item['path']]=item
        self.binary={}
        self.lstat={}
        for line in report['metadata'].splitlines():
            match=re.fullmatch(r'BINARY_SHA ([0-9a-f]{64})  (/[^\r\n]+)',line)
            if match:
                if match[2] in self.binary:raise UnsupportedProfile('duplicate_binary_record')
                self.binary[match[2]]=match[1]
            fields=line.split()
            if len(fields)>=9 and re.fullmatch(r'[-d][rwxstST-]{9}',fields[0]):
                path=fields[-1]
                if path.startswith('/'):
                    if path in self.lstat:raise UnsupportedProfile('duplicate_lstat_record')
                    self.lstat[path]=fields[:4]

    def regular(self,path,expected=None):
        item=self.files.get(path)
        if not item or item['type']!='regular' or item['uid']!=0 or item['gid']!=0 or item['mode']&0o022:
            raise UnsupportedProfile('unsafe_or_missing_file:'+path)
        data=self.content.get(item['local_name'])
        if data is None or _sha(data)!=item['sha256'] or len(data)!=item['size']:
            raise UnsupportedProfile('corrupt_snapshot:'+path)
        observed=self.lstat.get(path)
        if observed!=[stat.filemode(stat.S_IFREG|item['mode']),'1','0','0']:
            raise UnsupportedProfile('file_identity_not_confirmed:'+path)
        if expected and item['sha256']!=expected:raise UnsupportedProfile('file_profile_changed:'+path)
        return data

    def binary_pin(self,path,digest):
        if self.binary.get(path)!=digest:raise UnsupportedProfile('binary_profile_changed:'+path)
        fields=self.lstat.get(path)
        if not fields or fields[0][0]!='-' or fields[1:]!=['1','0','0']:
            raise UnsupportedProfile('binary_identity_not_confirmed:'+path)
        stock='/opt/inst/ota/canvas_gd32-20251029_184624-00.00.00.44-app.bin'
        if fields[0][8]=='w' or (fields[0][5]=='w' and path!=stock):raise UnsupportedProfile('writable_binary:'+path)

def _plan(data):
    result={}
    for line in data.decode('ascii').splitlines():
        match=re.fullmatch(r'([0-9a-f]{64}) ([0-9a-f]{64}) ([a-z-]+)',line)
        if not match or match[3] not in TARGETS or match[3] in result:raise UnsupportedProfile('invalid_legacy_backup_plan')
        result[match[3]]=(match[1],match[2])
    if set(result)!=set(TARGETS):raise UnsupportedProfile('incomplete_legacy_backup_plan')
    return result

def analyze_snapshot(report,content,catalogue=None):
    # The optional catalogue is for offline fixtures/profile compilation, never
    # supplied by an HTTP request. This offline planner is not an installation authority.
    if catalogue is None:catalogue=json.loads((HERE/'installed-profiles.json').read_text())
    view=Snapshot(report,content)
    if 'KERNEL '+catalogue['kernel'] not in report['metadata'].splitlines():raise UnsupportedProfile('unsupported_kernel')
    for path,digest in catalogue['binary_pins'].items():view.binary_pin(path,digest)
    for path,digest in catalogue['common_files'].items():view.regular(path,digest)
    for path in ['/etc/init.d','/opt/usr','/opt/usr/cc2-reactor-runtime-v2','/opt/usr/cc2-network-manager']:
        fields=view.lstat.get(path)
        if not fields or fields[0][0]!='d' or fields[2:]!=['0','0'] or fields[0][5]=='w' or fields[0][8]=='w':
            raise UnsupportedProfile('unsafe_startup_directory:'+path)
    choices=[]
    for variant in catalogue['variants']:
        if all(path in view.files and view.files[path].get('sha256')==digest for path,digest in variant['files'].items()):choices.append(variant)
    if len(choices)!=1:raise UnsupportedProfile('unknown_or_ambiguous_startup_profile')
    variant=choices[0]
    for path,digest in variant['files'].items():view.regular(path,digest)
    expected_maps=dict(catalogue['mapped_common'])
    optional_maps=dict(catalogue.get('mapped_optional',{}))
    if variant['legacy_rfid']:
        expected_maps.update(catalogue['mapped_legacy'])
        for path,digest in catalogue['mapped_legacy'].items():view.binary_pin(path,digest)
    lines=report['metadata'].splitlines()
    pids=[line.split()[1] for line in lines if re.fullmatch(r'NATIVE_PID [0-9]+',line)]
    if len(pids)!=1 or int(pids[0])<=1:raise UnsupportedProfile('native_process_count')
    running=[line for line in lines if line.startswith('RUNNING_SHA ')]
    expected_running='RUNNING_SHA '+catalogue['native_sha']+'  /proc/'+pids[0]+'/exe'
    if running!=[expected_running] or catalogue['native_path'] not in lines:raise UnsupportedProfile('running_native_identity')
    maps={line for line in lines if line.startswith('/') and line.endswith('.so')}
    if not set(expected_maps)<=maps or maps-set(expected_maps)-set(optional_maps):raise UnsupportedProfile('unknown_or_missing_native_module')
    for path in maps:view.binary_pin(path,{**expected_maps,**optional_maps}[path])
    preserved=[]
    if variant['legacy_rfid']:
        previous=None
        for index,stage in enumerate(STAGES):
            root='/opt/usr/'+stage
            expected=catalogue['legacy_plans'][stage]
            plan=_plan(view.regular(root+'/backup/plan',expected))
            if previous and any(plan[key][0]!=previous[key][1] for key in TARGETS):raise UnsupportedProfile('broken_legacy_backup_chain')
            for key,(old,new) in plan.items():view.regular(root+'/backup/'+key+'.old',old)
            if index==len(STAGES)-1:
                for key,path in TARGETS.items():view.regular(path,plan[key][1])
            previous=plan;preserved.append(root)
        ready=view.regular('/tmp/cc2-rfid-native-r2/ready')
        if len(ready)!=40:raise UnsupportedProfile('invalid_host_capability_size')
        magic,nonce,token,deadline,pid,phase=struct.unpack('<8sQQQII',ready)
        if magic!=b'CC2RF1\0\0' or not nonce or pid!=int(pids[0]) or phase not in [0,1,2]:raise UnsupportedProfile('host_capability_identity')
        ticket=view.regular('/opt/usr/cc2-canvas-rfid-trial-r1a/recovery-ticket')
        if len(ticket)!=80:raise UnsupportedProfile('legacy_canvas_ticket_size')
        magic,boot,issued,pid,stage,nonce=struct.unpack('<16s40sQIIQ',ticket)
        if magic!=b'CC2-CANVAS-R1\0\0\0' or stage!=4 or not issued or not nonce:raise UnsupportedProfile('legacy_canvas_completion_unknown')
    # First install/migration always starts guarded. No inference that a stored
    # legacy ACK is physical flash readback or that the MCU is currently stock.
    paths=['/etc/init.d/cc2-reactor-launch',*TARGETS.values(),'/opt/usr/cc2-network-manager/startup.sha256']
    targets=[]
    for path in paths:
        view.regular(path);item=view.files[path]
        targets.append({'path':path,'before_sha256':item['sha256'],'before_mode':item['mode']})
    return {
        'profile':variant['id'],'recognized':True,'recognition_only':True,
        'installation_authorized':False,'live_idle_cold_preflight_required':True,
        'initial_lifecycle_phase':'HOST_STAGED','first_boot_requires_guard':True,
        'legacy_canvas_ack_is_not_flash_readback':True,
        'preserve_roots':preserved+([] if catalogue.get('stock_bootstrap') else ['/opt/usr/cc2-cavity-floor'])+['/opt/usr/cc2-network-manager','/opt/usr/cc2-reactor-runtime-v2'],
        'startup_targets':targets,'delete_legacy_backups':False,
        'must_verify_current_run_printer_before_install':True,
        'startup_delegate_must_preserve_prepare_only':True,
        'vendor_fallback_must_be_gated':True,
        'api_state_in_snapshot_is_not_live_install_evidence':True,
        'snapshot_sha256':report['stream_sha256'],
    }
