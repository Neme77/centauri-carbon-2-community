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
assert not any(x['tag_name']==tag for x in api('releases?per_page=100')),'release already exists'
out=Path('release-output');out.mkdir();ci=out/'ci';ci.mkdir()
subprocess.run(['gh','run','download',str(d['build_run']),'-n',d['app_artifact'],'-D',str(ci)],check=True)
installer='CC2-Control-1.2.22-Multiplatform-Update.zip'
b=(ci/installer).read_bytes();assert sha(b)==d['app_files'][installer];(out/installer).write_bytes(b)
with zipfile.ZipFile(ci/installer) as z:
 prefix=installer[:-4]+'/'
 (out/'CC2-Control-1.2.22-Sources.zip').write_bytes(z.read(prefix+'cc2-control-sources.zip'))
 payload=gzip.decompress(z.read(prefix+'cc2-control-payload.tar.gz'))
assert len(payload)<=32*1024*1024 and len(payload)%512==0
(out/'CC2-Control-1.2.22-V4.4.cc2update').write_bytes(base64.b64decode(d['signed_header'],validate=True)+payload)
kit='CC2-Canvas-Pullback-Experimental-R4.zip';(out/kit).write_bytes((ci/kit).read_bytes())
(out/'update-channel.cc2catalog').write_bytes(base64.b64decode(d['catalog'],validate=True))
for name,encoded in d['small_files'].items():
 assert Path(name).name==name;(out/name).write_bytes(base64.b64decode(encoded,validate=True))
subprocess.run(['gh','release','download','CC2-Control-1.2.19-V4.4','--pattern','CC2-Camera-R2-Sources.zip','--dir',str(out)],check=True)
subprocess.run(['node',str(root/'verify.mjs'),str(out),str(root/'publication.json')],check=True)
for n,h in d['app_files'].items():assert sha((out/n).read_bytes())==h,n
subprocess.run(['gh','release','create',tag,'--target',d['target'],'--title','CC2 Control 1.2.22 — external spool, Spoolman and refreshed controls','--notes-file',str(out/'RELEASE-NOTES.md'),'--draft','--latest=false',*[str(out/n) for n in d['app_files']]],check=True)
def assets_ok():
 rel=api('releases/tags/'+tag)
 assert len(rel['assets'])==len(d['app_files'])
 for a in rel['assets']:assert a['name'] in d['app_files'] and a['digest']=='sha256:'+d['app_files'][a['name']],a['name']
 assert api('git/ref/tags/'+tag)['object']['sha']==d['target'],'tag not on main target'
 return rel
assert assets_ok()['draft']
subprocess.run(['gh','release','edit',tag,'--draft=false','--latest'],check=True)
assert not assets_ok()['draft'] and api('releases/latest')['tag_name']==tag
print('Published verified application release from main. Catalogue activation remains separate.')
