"""Publish only pinned tested assets. Signing keys are never read on CI."""
from pathlib import Path
import base64,gzip,hashlib,json,subprocess,sys,zipfile
root=Path(__file__).resolve().parent
data=json.loads((root/'publication.json').read_text(encoding='utf-8'))
repo='Neme77/centauri-carbon-2-community'
app_tag='CC2-Control-1.2.21-V4.4';fw_tag='V4.4.2'
def run(*args):return subprocess.check_output(['gh',*args],text=True)
def api(path):return json.loads(run('api','repos/'+repo+'/'+path))
def sha(b):return hashlib.sha256(b).hexdigest()
def releases():return api('releases?per_page=100')
def release(tag):return next(r for r in releases() if r['tag_name']==tag)
def assets_ok(tag,files):
 r=release(tag);assert len(r['assets'])==len(files),(tag,'unexpected asset count')
 for a in r['assets']:assert a['name'] in files and a['digest']=='sha256:'+files[a['name']],(tag,a['name'],'digest mismatch')
 return r
if sys.argv[1]=='draft':
 assert not any(r['tag_name'] in [app_tag,fw_tag] for r in releases()),'Release already exists'
 out=Path('release-output');out.mkdir();ci=out/'ci';ci.mkdir()
 for artifact in [data['app_artifact'],data['builder_artifact']]:
  subprocess.run(['gh','run','download',str(data['ci_run']),'-n',artifact,'-D',str(ci)],check=True)
 installer='CC2-Control-1.2.21-Multiplatform-Update.zip';b=(ci/installer).read_bytes();assert sha(b)==data['app_files'][installer];(out/installer).write_bytes(b)
 with zipfile.ZipFile(ci/installer) as z:
  prefix=installer[:-4]+'/'
  (out/'CC2-Control-1.2.21-Sources.zip').write_bytes(z.read(prefix+'cc2-control-sources.zip'))
  payload=gzip.decompress(z.read(prefix+'cc2-control-payload.tar.gz'))
 assert len(payload)<=32*1024*1024 and len(payload)%512==0
 (out/'CC2-Control-1.2.21-V4.4.cc2update').write_bytes(base64.b64decode(data['signed_header'],validate=True)+payload)
 builder='CC2-Builder-V4.4.2-Control-1.2.21.zip'
 (out/builder).write_bytes((ci/'CC2-Builder-V4.4.2-Control-1.2.21-Test-R4.zip').read_bytes())
 (out/'update-channel.cc2catalog').write_bytes(base64.b64decode(data['catalog'],validate=True))
 for name,encoded in data['small_files'].items():
  assert Path(name).name==name;(out/name).write_bytes(base64.b64decode(encoded,validate=True))
 subprocess.run(['gh','release','download','CC2-Control-1.2.19-V4.4','--pattern','CC2-Camera-R2-Sources.zip','--dir',str(out)],check=True)
 subprocess.run(['node',str(root/'verify.mjs'),str(out),str(root/'publication.json')],check=True)
 for tag,title,notes,files in [(fw_tag,'Community Firmware V4.4.2 — Control 1.2.21 + integrated camera fix','FIRMWARE-NOTES.md',data['firmware_files']),(app_tag,'CC2 Control 1.2.21 — Canvas, independent experiments and reliable updates','RELEASE-NOTES.md',data['app_files'])]:
  present=[str(out/n) for n in files if (out/n).is_file()]
  for n in files:
   if (out/n).is_file():assert sha((out/n).read_bytes())==files[n],n
  subprocess.run(['gh','release','create',tag,'--target',data['target'],'--title',title,'--notes-file',str(out/notes),'--draft','--latest=false',*present],check=True)
 print('Drafts created. Upload local tested firmware and both app screenshots before publication.')
elif sys.argv[1]=='publish':
 checks=api('commits/'+data['ci_head']+'/check-runs')['check_runs']
 required={'CC2 Control (host tests + ARM build)','CC2 Control (address,undefined sanitizer)','CC2 Control (thread sanitizer)','Firmware builder (host tests)','Experimental reactor (host fixtures)','Web UI (type-check, build, up to date)','payload-44','preview'}
 assert required <= {c['name'] for c in checks if c['conclusion']=='success'},'Final checks not all green'
 assert all(c['status']=='completed' and c['conclusion'] in ['success','skipped'] for c in checks)
 pr=api('pulls/214');assert pr['merged'] and pr['head']['sha']==data['ci_head'],'PR must be merged at verified head'
 for tag,files in [(fw_tag,data['firmware_files']),(app_tag,data['app_files'])]:
  r=assets_ok(tag,files);assert r['draft'] and not r['prerelease']
 subprocess.run(['gh','release','edit',fw_tag,'--draft=false','--latest=false'],check=True)
 subprocess.run(['gh','release','edit',app_tag,'--draft=false','--latest'],check=True)
 for tag,files in [(fw_tag,data['firmware_files']),(app_tag,data['app_files'])]:assert not assets_ok(tag,files)['draft']
 assert api('releases/latest')['tag_name']==app_tag
 print('Published both verified releases; stable app is Latest. Catalogue activation is a separate verified source change.')
else:raise SystemExit('Unknown publication phase')
