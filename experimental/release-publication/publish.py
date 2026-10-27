from pathlib import Path
import base64,gzip,hashlib,json,subprocess,zipfile
root=Path(__file__).resolve().parent
d=json.loads((root/'publication.json').read_text(encoding='utf-8'))
repo='Neme77/centauri-carbon-2-community';tag='CC2-Control-1.2.23-V4.4'
def run(*args):return subprocess.check_output(['gh',*args],text=True)
def api(p):return json.loads(run('api','repos/'+repo+'/'+p))
def sha(b):return hashlib.sha256(b).hexdigest()
assert api('git/ref/heads/main')['object']['sha']==d['target'],'main moved'
assert api('git/commits/'+d['target'])['tree']['sha']==d['target_tree']
assert api('git/commits/'+d['ci_head'])['tree']['sha']==d['target_tree'],'main differs from verified release'
assert api('compare/'+d['ci_head']+'...'+d['target'])['status'] in ['ahead','identical']
checks=api('commits/'+d['ci_head']+'/check-runs?filter=latest&per_page=100')['check_runs']
required={'CC2 Control (host tests + ARM build)','CC2 Control (address,undefined sanitizer)','CC2 Control (thread sanitizer)','Firmware builder (host tests)','Experimental reactor (host fixtures)','Web UI (type-check, build, up to date)','payload-44'}
assert required <= {c['name'] for c in checks if c['conclusion']=='success'},'final CI incomplete'
assert all(c['status']=='completed' and c['conclusion'] in ['success','skipped'] for c in checks),'a final check failed'
assert api('pulls/'+str(d['release_pr']))['merged']
releases=api('releases?per_page=100');matches=[r for r in releases if r['tag_name']==tag]
assert not matches or len(matches)==1 and matches[0]['draft'],'public release already exists'
out=Path('release-output');out.mkdir(exist_ok=True);ci=out/'ci';ci.mkdir(exist_ok=True)
subprocess.run(['gh','run','download',str(d['build_run']),'-n',d['app_artifact'],'-D',str(ci)],check=True)
installer='CC2-Control-1.2.23-Multiplatform-Update.zip'
b=(ci/installer).read_bytes();assert sha(b)==d['app_files'][installer];(out/installer).write_bytes(b)
with zipfile.ZipFile(ci/installer) as z:
 prefix=installer[:-4]+'/'
 (out/'CC2-Control-1.2.23-Sources.zip').write_bytes(z.read(prefix+'cc2-control-sources.zip'))
 payload=gzip.decompress(z.read(prefix+'cc2-control-payload.tar.gz'))
assert len(payload)<=32*1024*1024 and len(payload)%512==0
(out/'CC2-Control-1.2.23-V4.4.cc2update').write_bytes(base64.b64decode(d['signed_header'],validate=True)+payload)
(out/'update-channel.cc2catalog').write_bytes(base64.b64decode(d['catalog'],validate=True))
for name,encoded in d['small_files'].items():
 assert Path(name).name==name;(out/name).write_bytes(base64.b64decode(encoded,validate=True))
subprocess.run(['gh','release','download','CC2-Control-1.2.22-V4.4','--pattern','CC2-Camera-R2-Sources.zip','--dir',str(out)],check=True)
subprocess.run(['node',str(root/'verify.mjs'),str(out),str(root/'publication.json')],check=True)
for n,h in d['app_files'].items():assert sha((out/n).read_bytes())==h,n
if not matches:
 subprocess.run(['gh','release','create',tag,'--target',d['target'],'--title','CC2 Control 1.2.23 - TPU WITHOUT UNPLUGGING CANVAS','--notes-file',str(out/'RELEASE-NOTES.md'),'--draft','--latest=false',*[str(out/n) for n in d['app_files']]],check=True)
releases=api('releases?per_page=100');r=next(r for r in releases if r['tag_name']==tag)
assert r['draft'] and r['target_commitish']==d['target'] and not r['prerelease']
assert len(r['assets'])==len(d['app_files'])
for a in r['assets']:assert a['digest']=='sha256:'+d['app_files'][a['name']],a['name']
assert r['body'].strip()==(out/'RELEASE-NOTES.md').read_text(encoding='utf-8').strip()
request=root/'publish-request.json';request.write_text(json.dumps({'draft':False,'make_latest':'true'}),encoding='utf-8')
run('api','--method','PATCH','repos/'+repo+'/releases/'+str(r['id']),'--input',str(request))
r=api('releases/'+str(r['id']));assert not r['draft'] and not r['prerelease']
assert api('git/ref/tags/'+tag)['object']['sha']==d['target'],'tag not on main'
assert api('releases/latest')['tag_name']==tag
for a in r['assets']:assert a['digest']=='sha256:'+d['app_files'][a['name']],a['name']
print('Published verified 1.2.23 assets from main. Catalogue activation remains separate.')
