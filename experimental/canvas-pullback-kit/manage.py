#!/usr/bin/env python3
"""Controlled-test installer; SSH only, no vendor program in the distribution."""
import argparse
import io
import json
import math
import re
import secrets
import shlex
import subprocess
import sys
import tarfile
from pathlib import Path
from policy import BASE, STATE, HOOK, TARGETS, CONFIGS, MODES, digest, patched_files, clamp_config, validate_tar_member, select_profile
from patch_pullback import ORIGINAL_SHA256, EXTENDED_SHA256

HOME = Path(__file__).resolve().parent
META = {'printer.json','health.json','pid','exe','running.sha','checks','cmdline','activation','mapped','stat'}
ALLOWED = set(TARGETS.values()) | set(CONFIGS.values()) | {BASE+'/firmware-payload.sha256', '/opt/inst/cc2-reactor/SHA256SUMS', STATE+'/manifest.json'} | {STATE+'/backup/'+k for k in TARGETS}

class Remote:
    def __init__(self, host, user='root', port=22):
        if not re.fullmatch(r'[A-Za-z0-9][A-Za-z0-9.-]{0,252}',host) or not re.fullmatch(r'[a-z_][a-z0-9_-]{0,31}',user):
            raise ValueError('Use a plain hostname/IPv4 address and a valid SSH username')
        if not 1 <= port <= 65535:
            raise ValueError('Invalid SSH port')
        self.command=['ssh','-o','ConnectTimeout=15','-o','ServerAliveInterval=15','-o','ServerAliveCountMax=3','-p',str(port),user+'@'+host]
    def run(self, command, data):
        result=subprocess.run(self.command+[command],input=data,stdout=subprocess.PIPE,stderr=subprocess.PIPE,check=False)
        if result.returncode:
            raise RuntimeError(result.stderr.decode('utf-8','replace')[-6000:]+'\n'+result.stdout.decode('utf-8','replace')[-3000:])
        return result.stdout
    def snapshot(self):
        return unpack(self.run('sh -s', (HOME/'snapshot.sh').read_bytes()))

def unpack(raw):
    if len(raw) > 100*1024*1024:
        raise ValueError('Snapshot exceeds the supported size')
    files, modes, metadata = {}, {}, {}
    with tarfile.open(fileobj=io.BytesIO(raw),mode='r:') as archive:
        for member in archive:
            name=validate_tar_member(member)
            match=re.fullmatch(r'/tmp/cc2-pb-read\.[A-Za-z0-9]+/([^/]+)',name)
            if match and match[1] in META:
                key=match[1]
                if key in metadata or member.size > 32768:
                    raise ValueError('Duplicate/oversized metadata')
                metadata[key]=archive.extractfile(member).read()
            elif name in ALLOWED:
                if name in files or member.mode & 0o7000:
                    raise ValueError('Duplicate or privileged snapshot file')
                files[name]=archive.extractfile(member).read();modes[name]=member.mode & 0o777
            else:
                raise ValueError('Unexpected snapshot member: '+name)
    if set(metadata) != META or not set(TARGETS.values()).issubset(files) or not {CONFIGS['main'],CONFIGS['autosave']}.issubset(files):
        raise ValueError('Incomplete snapshot')
    return files,modes,metadata

def qualify(snapshot):
    files,modes,meta=snapshot
    pid=meta['pid'].decode().strip()
    if not re.fullmatch(r'[0-9]+',pid):raise ValueError('Invalid PID')
    if meta['exe'].decode().strip().removesuffix(' (deleted)') != TARGETS['program']:
        raise ValueError('Unsupported running executable path')
    running=meta['running.sha'].decode().split()[0]
    if running not in (ORIGINAL_SHA256,EXTENDED_SHA256):raise ValueError('Unsupported running printer program')
    if not re.fullmatch(rb'pid='+pid.encode()+rb' callback=0x[0-9a-f]+ timers=0x[0-9a-f]+ poll=0x[0-9a-f]+ wait=0x[0-9a-f]+\n?',meta['activation']):
        raise ValueError('Live callback activation is not verified')
    if (BASE+'/libcc2-reactor-v2.so').encode() not in meta['mapped']:
        raise ValueError('Qualified callback library is not mapped')
    args=meta['cmdline'].split(b'\0')
    if CONFIGS['main'].encode() not in args or CONFIGS['autosave'].encode() not in args:
        raise ValueError('Unsupported printer configuration arguments')
    try:
        start=meta['stat'].decode().rsplit(')',1)[1].split()[19]
        assert start.isdigit()
    except (IndexError,AssertionError):raise ValueError('Cannot verify process start time')
    profile,pins=select_profile(files.get('/opt/inst/cc2-reactor/SHA256SUMS',b''), files.get(BASE+'/firmware-payload.sha256',b''))
    checks={line.split(None,1)[1].strip():line.split()[0] for line in meta['checks'].decode().splitlines()}
    expected={'/opt/bin/elegoo_printer':ORIGINAL_SHA256,'/opt/lib/libcolib.so':pins['libco'],**{BASE+'/'+k:pins[k] for k in ('preflight.sh','libcc2-reactor-v2.so','cc2-reactor-bridge-test')}}
    if checks != expected:raise ValueError('Native program/libco/runtime fingerprint mismatch')
    installed=STATE+'/manifest.json' in files
    original={k:files[STATE+'/backup/'+k] if installed else files[p] for k,p in TARGETS.items()}
    extended=patched_files(original,pins)
    if installed:
        manifest=json.loads(files[STATE+'/manifest.json'])
        if manifest != component_manifest(original,extended):raise ValueError('Component backup metadata differs')
    for key,path in TARGETS.items():
        allowed=(digest(original[key]),digest(extended[key])) if installed else (pins[key],)
        if digest(files[path]) not in allowed:raise ValueError('Changed or unsupported installed file: '+key)
    for path in CONFIGS.values():
        if path in files:clamp_config(files[path])
    return {'profile':profile,'pid':pid,'start':start,'running':running,'installed':installed,'original':original,'extended':extended,'checks':checks}

def ready(meta):
    p=json.loads(meta['printer.json']);h=json.loads(meta['health.json'])
    number=lambda x:type(x) in (int,float) and math.isfinite(x)
    age=p.get('last_message_age')
    if p.get('connected') is not True or type(p.get('machine',{}).get('status')) is not int or p['machine']['status']!=1 or not number(age) or not 0<=age<=15:
        raise ValueError('Printer must be connected, Idle and freshly reported')
    if p.get('print',{}).get('enabled') is not False or p.get('print',{}).get('state') in ('printing','paused'):
        raise ValueError('An active or paused print prevents installation/removal')
    if h.get('mqtt_registered') is not True or h.get('version')!='1.2.21':
        raise ValueError('This pilot requires registered CC2 Control 1.2.21')
    for key in ('extruder','heater_bed'):
        item=p.get(key,{})
        if not number(item.get('temperature')) or not 0<=item['temperature']<=50 or not number(item.get('target')) or item['target']!=0:
            raise ValueError('Printer must be cold with heater targets zero')

def component_manifest(original,extended):
    return {'component':'cc2-canvas-pullback-1000','schema':1,'credit':'Noahwhite2014 #172',
            'before':{k:digest(v) for k,v in original.items()},'after':{k:digest(v) for k,v in extended.items()}}

def prepare(snapshot, info, action):
    files,modes,meta=snapshot
    ready(meta)
    if action=='install' and info['installed']:raise ValueError('Component already exists: inspect status or remove it first')
    if action=='remove' and not info['installed']:raise ValueError('No qualified component backup was found')
    if action=='install':
        for path in CONFIGS.values():
            if path in files:
                for value in clamp_config(files[path])[1]:
                    if value>350:
                        shown=str(value)
                        if len(shown)>80:shown=shown[:80]+'...'
                        raise ValueError(f'Installation requires every configured pull-back value at most 350 mm: {path}: unload_fila_dist={shown}')
        order=list(TARGETS)
        changed={k:(TARGETS[k],info['extended'][k],MODES[k]) for k in order}
    else:
        # Normalize all supported config layers BEFORE the unpatched program can be restored.
        changed={}
        for key,path in CONFIGS.items():
            if path in files:
                safe,values=clamp_config(files[path])
                if safe!=files[path]:changed[key]=(path,safe,modes[path])
        for key in ('program','sums','launcher','boot'):
            changed[key]=(TARGETS[key],info['original'][key],MODES[key])
    before={p:digest(data) for p,data in files.items() if p in set(TARGETS.values())|set(CONFIGS.values())|{BASE+'/firmware-payload.sha256','/opt/inst/cc2-reactor/SHA256SUMS'}}
    before.update(info['checks']);before['/proc/'+info['pid']+'/exe']=info['running']
    plan='';after={};payload={'transaction.sh':(HOME/'transaction.sh').read_bytes(),'pid':info['pid'].encode()+b'\n','start':info['start'].encode()+b'\n'}
    for key,(path,data,mode) in changed.items():
        plan+=f'{key}|{path}|{digest(files[path])}|{digest(data)}|{mode:03o}\n'
        after[path]=digest(data);payload['after/'+key]=data
    payload['plan']=plan.encode()
    payload['before.sha256']=''.join(h+'  '+p+'\n' for p,h in before.items()).encode()
    payload['after.sha256']=''.join(h+'  '+p+'\n' for p,h in after.items()).encode()
    payload['manifest.json']=(json.dumps(component_manifest(info['original'],info['extended']),sort_keys=True,indent=2)+'\n').encode()
    payload['payload.sha256']=''.join(digest(data)+'  '+name+'\n' for name,data in payload.items()).encode()
    return payload

def archive_payload(payload):
    out=io.BytesIO()
    with tarfile.open(fileobj=out,mode='w') as archive:
        for name,data in payload.items():
            member=tarfile.TarInfo(name);member.size=len(data);member.mode=0o600;member.mtime=0
            archive.addfile(member,io.BytesIO(data))
    return out.getvalue()

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('action',choices=['check','status','install','remove'])
    parser.add_argument('--host');parser.add_argument('--user',default='root');parser.add_argument('--port',type=int,default=22)
    args=parser.parse_args()
    host=args.host or input('Printer IP / IP stampante: ').strip()
    remote=Remote(host,args.user,args.port)
    snapshot=remote.snapshot();info=qualify(snapshot)
    print('Qualified native/runtime fingerprints. / Impronte verificate.', info['profile'])
    print('Component:', 'present / presente' if info['installed'] else 'absent / assente')
    print('Running program:', 'extended / esteso' if info['running']==EXTENDED_SHA256 else 'standard')
    if info['installed']:
        state='extended' if all(snapshot[0][p]==info['extended'][k] for k,p in TARGETS.items()) else 'original' if all(snapshot[0][p]==info['original'][k] for k,p in TARGETS.items()) else 'partial'
        print('Files on disk:',state)
        if state=='partial':print('Interrupted transaction: remove using the qualified backup before reboot.')
        elif (state=='extended') != (info['running']==EXTENDED_SHA256):print('Reboot required: disk and running executable differ.')
    if args.action=='status':return
    ready(snapshot[2])
    if args.action=='check':
        if info['installed']:print('Inspect/remove the existing component before reinstalling. / Verificare o rimuovere prima il componente presente.')
        else:prepare(snapshot,info,'install');print('CHECK PASS. No persistent change, movement or reboot. / Nessuna modifica persistente, movimento o riavvio.')
        return
    print('EXPERIMENTAL. Physical 1000 mm pull-back/reload is not qualified. / Prova fisica a 1000 mm non validata.')
    print('Remove this component and reboot before app/firmware updates. / Rimuovere e riavviare prima degli aggiornamenti.')
    print('Removal clamps values above 350 mm in supported configuration files. / La rimozione riporta a 350 mm i valori superiori.')
    phrase='INSTALL PULLBACK' if args.action=='install' else 'REMOVE PULLBACK'
    if input('Type / Scrivi '+phrase+': ').strip()!=phrase:print('Cancelled / Annullato');return
    payload=prepare(snapshot,info,args.action)
    stage='/opt/usr/.cc2-pullback-stage-'+secrets.token_hex(10)
    command='umask 077; mkdir '+shlex.quote(stage)+' && tar -xf - -C '+shlex.quote(stage)+' && sh '+shlex.quote(stage+'/transaction.sh')+' '+args.action
    print(remote.run(command,archive_payload(payload)).decode('utf-8','replace'))
    print('Verify after reboot using status, then Canvas, local/Matrix camera and normal loading. / Verificare dopo il riavvio.')

if __name__=='__main__':
    try:main()
    except (OSError,ValueError,RuntimeError,KeyError,tarfile.TarError) as error:
        print('STOP:',error,file=sys.stderr);sys.exit(1)
    except KeyboardInterrupt:
        print('Interrupted. No movement requested. If a transaction began, inspect status before reboot. / Interrotto: verificare lo stato.',file=sys.stderr);sys.exit(130)
