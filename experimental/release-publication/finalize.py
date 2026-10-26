"""Publish checksum-pinned application release; private signing key never enters CI."""
from pathlib import Path
import base64,gzip,hashlib,json,subprocess,zipfile
root=Path(__file__).resolve().parent
d=json.loads((root/'publication.json').read_text(encoding='utf-8'))
repo='Neme77/centauri-carbon-2-community';tag='CC2-Control-1.2.22-V4.4'
def run(*args):return subprocess.check_output(['gh',*args],text=True)
def api(p):return json.loads(run('api','repos/'+repo+'/'+p))
def sha(b):return hashlib.sha256(b).hexdigest()
assert api('git/ref/heads/main')['object']['sha']==d['target'],'main moved'
assert api('git/commits/'+d['target'])['tree']['sha']==d['target_tree']
assert api('git/commits/'+d['ci_head'])['tree']['sha']==d['target_tree'],'main tree differs from verified release'
assert api('compare/'+d['ci_head']+'...'+d['target'])['status'] in ['ahead','identical'],'tag target must include checked release'
checks=api('commits/'+d['ci_head']+'/check-runs?filter=latest&per_page=100')['check_runs']
required={'CC2 Control (host tests + ARM build)','CC2 Control (address,undefined sanitizer)','CC2 Control (thread sanitizer)','Firmware builder (host tests)','Experimental reactor (host fixtures)','Web UI (type-check, build, up to date)','payload-44'}
assert required <= {c['name'] for c in checks if c['conclusion']=='success'},'final CI incomplete'
assert all(c['status']=='completed' and c['conclusion'] in ['success','skipped'] for c in checks),'a final check failed'
pr=api('pulls/'+str(d['release_pr']));assert pr['merged'] and pr['head']['sha']==d['ci_head']
releases=api('releases?per_page=100')
matches=[r for r in releases if r['tag_name']==tag]
assert len(matches)==1,'exactly one prepared draft required'
r=matches[0];assert r['draft'] and not r['prerelease']
assert r['target_commitish']==d['target'],'draft target differs from checked main'
assert len(r['assets'])==len(d['app_files'])
for a in r['assets']:
 assert a['name'] in d['app_files'] and a['digest']=='sha256:'+d['app_files'][a['name']],a['name']
notes=base64.b64decode(d['small_files']['RELEASE-NOTES.md'],validate=True).decode()
assert r['body'].strip()==notes.strip(),'release notes differ'
# GitHub does not expose draft releases by tag or create their tag before publication.
# Locate the authenticated draft by release ID; do not replace any uploaded asset.
request=root/'publish-request.json';request.write_text(json.dumps({'draft':False,'make_latest':'true'}),encoding='utf-8')
run('api','--method','PATCH','repos/'+repo+'/releases/'+str(r['id']),'--input',str(request))
r=api('releases/'+str(r['id']));assert not r['draft'] and not r['prerelease']
assert api('git/ref/tags/'+tag)['object']['sha']==d['target'],'published tag not on checked main'
assert api('releases/latest')['tag_name']==tag
for a in r['assets']:assert a['digest']=='sha256:'+d['app_files'][a['name']],a['name']
print('Published all 16 verified assets from main; no existing file replaced.')
