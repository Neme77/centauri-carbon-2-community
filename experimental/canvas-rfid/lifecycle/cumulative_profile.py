"""Expected post-EXHAUST/pre-RFID baseline from a recognized clean snapshot.

This is offline planning, not live installation authorization. The EXHAUST
manager must first verify and commit its own exact baseline on the printer;
the RFID publisher subsequently checks these exact resulting dependencies.
"""
import copy
import hashlib
import json
from pathlib import Path
import stat
from installed_profiles import analyze_snapshot, TARGETS
from migration_plan import update_manifest

HERE=Path(__file__).resolve().parent
FLOOR='/opt/usr/cc2-cavity-floor'
MANIFEST_SHA='7a08e95b2e92dcd93b7cdba4353001f71927f3f2955343205c607553981afa28'
sha=lambda data:hashlib.sha256(data).hexdigest()

def prepare(report,content,floor):
    catalogue=json.loads((HERE/'installed-profiles-stock.json').read_text())
    analyze_snapshot(report,content,catalogue)
    expected={}
    assert sha(floor['SHA256SUMS'])==MANIFEST_SHA,'Unqualified EXHAUST payload'
    for line in floor['SHA256SUMS'].decode('ascii').splitlines():
        digest,name=line.split('  ',1)
        assert name not in expected and sha(floor[name])==digest
        expected[name]=digest
    assert set(floor)==set(expected)|{'SHA256SUMS'}
    result=copy.deepcopy(report);data=dict(content)
    original={f['path']:f for f in result['files']}
    changed={}
    def read(path):return data[original[path]['local_name']]
    for key,name in [('reactor-launcher','launcher.sh'),('network-profile','profile.sh'),
                     ('network-entry','reactor-entry.sh'),('network-activation','activate.sh')]:
        changed[TARGETS[key]]=floor[name]
    changed[TARGETS['reactor-manifest']]=update_manifest(read(TARGETS['reactor-manifest']),{
        'launcher.sh':(original[TARGETS['reactor-launcher']]['sha256'],sha(floor['launcher.sh']))})
    changed[TARGETS['network-manifest']]=update_manifest(read(TARGETS['network-manifest']),{
        name:(original[TARGETS[key]]['sha256'],sha(floor[name])) for key,name in
        [('network-profile','profile.sh'),('network-entry','reactor-entry.sh'),('network-activation','activate.sh')]})
    changed[FLOOR+'/enabled']=b'CC2-CAVITY-FLOOR-R1\n'
    for path,value in changed.items():
        if path not in original:
            item={'path':path,'mode':0o600,'uid':0,'gid':0,'type':'regular','mtime':0,
                  'local_name':sha(path.encode())[:24]+'.bin'}
            result['files'].append(item);original[path]=item
        item=original[path];item.update(size=len(value),sha256=sha(value));data[item['local_name']]=value
    # Replace metadata identity rows, never pretend this synthetic plan was a
    # second physical inspection or is permission for native actions.
    lines=[]
    for line in result['metadata'].splitlines():
        if any(line.endswith(' '+path) for path in changed):continue
        lines.append(line)
    for path in changed:
        item=original[path]
        lines.extend([f'{stat.filemode(stat.S_IFREG|item["mode"])} 1 0 0 {item["size"]} Jan 1 00:00 {path}',
                      'FILE_SHA '+item['sha256']+'  '+path])
    library=FLOOR+'/libcc2-cavity-floor-r1.so'
    lines.extend(['-rw-r--r-- 1 0 0 '+str(len(floor['libcc2-cavity-floor-r1.so']))+' Jan 1 00:00 '+library,
                  'BINARY_SHA '+expected['libcc2-cavity-floor-r1.so']+'  '+library])
    result['metadata']='\n'.join(lines)+'\n'
    catalogue['scope']='Expected baseline after the qualified EXHAUST manager commits on a clean 4.4.3; initial module mapping optional, mandatory after full boot'
    catalogue['requires_cavity']=True
    catalogue['common_files'][FLOOR+'/enabled']=sha(changed[FLOOR+'/enabled'])
    catalogue['binary_pins'][library]=expected['libcc2-cavity-floor-r1.so']
    catalogue['mapped_optional'][library]=expected['libcc2-cavity-floor-r1.so']
    catalogue['variants']=[{'id':'reactor-network-cavity-stock-443','legacy_rfid':False,
        'files':{path:original[path]['sha256'] for path in TARGETS.values()}}]
    recognized=analyze_snapshot(result,data,catalogue)
    assert FLOOR in recognized['preserve_roots']
    return {'report':result,'content':data,'catalogue':catalogue,
            'expected_after_floor_only':True,'live_installation_authorized':False}
