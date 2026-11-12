"""Closed same-native-profile app rebind planning; no live action or signing.

Inputs must be source-qualified component runtimes. This module validates their
complete inventories and produces a pinned C plan. It does not grant
permission to run an update or replace the worker's live preflight.
"""
import hashlib,io,json,tarfile
from pathlib import PurePosixPath

ROOT='/opt/usr/cc2-rfid'
NAMES=('coordinator','capture','app-integration.json','activate-on-ready')
TRANSFER='transfer-worker'
EXECUTABLES={'coordinator','capture',TRANSFER}
KEYS={'protocol','app_sha256','coordinator_sha256','capture_sha256','profile_sha256'}
sha=lambda data:hashlib.sha256(data).hexdigest()

def hex64(s):return isinstance(s,str) and len(s)==64 and all(c in '0123456789abcdef' for c in s)

def unique_pairs(pairs):
    out={}
    for k,v in pairs:
        if k in out:raise ValueError('Duplicate metadata field')
        out[k]=v
    return out

def read_runtime(path):
    data=path.read_bytes()
    if not data or len(data)>8*1024*1024:raise ValueError('Unbounded runtime')
    files={};modes={};total=0
    with tarfile.open(fileobj=io.BytesIO(data),mode='r:gz') as archive:
        for item in archive:
            p=PurePosixPath(item.name);total+=item.size
            if (not item.isfile() or str(p)!=item.name or p.is_absolute() or '..' in p.parts or
                item.name in files or item.size<0 or item.size>4*1024*1024 or total>8*1024*1024 or
                item.mode not in (0o600,0o700) or item.uid or item.gid):raise ValueError('Unsafe runtime member')
            files[item.name]=archive.extractfile(item).read();modes[item.name]=item.mode
            if len(files)>48:raise ValueError('Too many runtime members')
    manifest={}
    for row in files.get('SHA256SUMS',b'').decode('ascii').splitlines():
        digest,name=row.split('  ',1)
        if not hex64(digest) or name in manifest:raise ValueError('Invalid runtime manifest')
        manifest[name]=digest
    if set(manifest)!=set(files)-{'SHA256SUMS'} or any(sha(files[n])!=h for n,h in manifest.items()):raise ValueError('Runtime manifest mismatch')
    if any(n not in ('publisher','SHA256SUMS') and not n.startswith('component/') for n in files):raise ValueError('Unexpected runtime root member')
    component={n.removeprefix('component/'):b for n,b in files.items() if n.startswith('component/')}
    permissions={n.removeprefix('component/'):m for n,m in modes.items() if n.startswith('component/')}
    metadata=json.loads(component['app-integration.json'],object_pairs_hook=unique_pairs)
    if set(metadata)!=KEYS or type(metadata['protocol']) is not int or metadata['protocol']!=1:raise ValueError('Unsupported integration metadata')
    if any(not hex64(metadata[k]) for k in KEYS-{'protocol'}):raise ValueError('Invalid integration digest')
    for key,name in [('coordinator_sha256','coordinator'),('capture_sha256','capture'),('profile_sha256','recovery-profile')]:
        if sha(component[name])!=metadata[key]:raise ValueError('Unbound integration artifact')
    request=f'CC2-RFID-ACTIVATE-1\n{metadata["app_sha256"]}\n{metadata["profile_sha256"]}\n'.encode()
    if component['activate-on-ready']!=request:raise ValueError('Unbound activation request')
    for i,n in enumerate(NAMES):
        if permissions[n]!=(0o700 if i<2 else 0o600):raise ValueError('Unsafe binding target mode')
    return component,permissions,metadata

def plan_components(old,old_modes,old_metadata,new,new_modes,new_metadata):
    if set(old)!=set(new) or old_modes!=new_modes:raise ValueError('Native component inventory or modes changed')
    changed={n for n in old if old[n]!=new[n]}
    names=NAMES+(TRANSFER,) if TRANSFER in changed else NAMES
    if not changed or not changed<=set(names):raise ValueError('Native profile migration is not an app rebind')
    if old_metadata['app_sha256']==new_metadata['app_sha256']:raise ValueError('App did not change')
    if old_metadata['profile_sha256']!=new_metadata['profile_sha256'] or old['recovery-profile']!=new['recovery-profile']:raise ValueError('Recovery profile changed')
    old_app=old_metadata['app_sha256'];new_app=new_metadata['app_sha256'];profile=old_metadata['profile_sha256']
    text=f'CC2RFUP1 {old_app} {new_app} {profile}\n';rows=[]
    for name in names:
        mode=0o700 if name in EXECUTABLES else 0o600
        if old_modes[name]!=mode or new_modes[name]!=mode:raise ValueError('Wrong binding mode')
        before=sha(old[name]);after=sha(new[name])
        text+=f'{name} {mode:04o} {before} {after}\n'
        rows.append('{%s,%s,%s,0%o}'%(json.dumps(name),json.dumps(before),json.dumps(after),mode))
    header=('static const struct au_plan au_expected={%s,%s,%s,{%s}};\n'%(
        json.dumps(old_app),json.dumps(new_app),json.dumps(profile),','.join(rows))).encode()
    return {'plan_sha256':sha(text.encode()),'old_app_sha256':old_app,'new_app_sha256':new_app,
        'profile_sha256':profile,'configuration':header,'incoming':{n:new[n] for n in names},
        'native_component_unchanged':TRANSFER not in changed,'Canvas_write':False,'live_installation_authorized':False}

def plan(old_path,new_path):
    old,om,metadata=read_runtime(old_path);new,nm,new_metadata=read_runtime(new_path)
    return plan_components(old,om,metadata,new,nm,new_metadata)

def descriptor(worker,planned):
    if len(worker)<20 or worker[:6]!=b'\x7fELF\x01\x01' or worker[18:20]!=b'\x28\x00':raise ValueError('Configured worker must be ARM32 little-endian ELF')
    if len(worker)>1024*1024:raise ValueError('Configured worker exceeds qualified bound')
    return (json.dumps({'protocol':1,'app_sha256':planned['new_app_sha256'],
        'worker_sha256':sha(worker),'plan_sha256':planned['plan_sha256']},separators=(',',':'))+'\n').encode()
