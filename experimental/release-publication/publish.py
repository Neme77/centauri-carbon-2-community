"""Publish a checksum-pinned prerelease; no private signing material is present."""
from pathlib import Path
import base64,gzip,hashlib,json,subprocess,zipfile
root=Path(__file__).resolve().parent
data=json.loads((root/'publication.json').read_text())
repo='Neme77/centauri-carbon-2-community'
def gh(*args):return subprocess.check_output(['gh',*args],text=True)
def api(path):return json.loads(gh('api',f'repos/{repo}/'+path))
assert api('releases/latest')['tag_name']==data['stable_tag']
assert api('contents/cc2-control/update-channel.cc2catalog?ref=develop')['sha']==data['stable_catalog_blob']
checks=api('commits/'+data['ci_head']+'/check-runs')['check_runs']
required={'CC2 Control (host tests + ARM build)','CC2 Control (address,undefined sanitizer)','CC2 Control (thread sanitizer)','Firmware builder (host tests)','Experimental reactor (host fixtures)','Web UI (type-check, build, up to date)','payload-44','preview'}
assert required<={c['name'] for c in checks if c['conclusion']=='success'}
assert all(c['status']=='completed' and c['conclusion'] in ('success','skipped') for c in checks)
tree=api('git/trees/'+data['target'])['tree']
assert next(x for x in tree if x['path']=='cc2-control')['sha']==data['application_tree']
out=Path('experimental-release-output');out.mkdir()
ci=out/'ci';ci.mkdir()
subprocess.run(['gh','run','download',str(data['ci_run']),'-n',data['artifact_name'],'-D',str(ci)],check=True)
name='CC2-Control-1.2.20-Multiplatform-Update.zip'
b=(ci/name).read_bytes();assert hashlib.sha256(b).hexdigest()==data['files'][name]
(out/name).write_bytes(b)
with zipfile.ZipFile(ci/name) as z:
 prefix=name[:-4]+'/'
 source=z.read(prefix+'cc2-control-sources.zip')
 payload=gzip.decompress(z.read(prefix+'cc2-control-payload.tar.gz'))
assert len(payload)<=32*1024*1024 and len(payload)%512==0
assert hashlib.sha256(source).hexdigest()==data['source_archive_sha256']
(out/'CC2-Control-1.2.20-Sources.zip').write_bytes(source)
(out/'CC2-Control-1.2.20-V4.4.cc2update').write_bytes(base64.b64decode(data['signed_header'],validate=True)+payload)
(out/'update-channel.cc2catalog').write_bytes(base64.b64decode(data['catalog'],validate=True))
subprocess.run(['gh','release','download',data['stable_tag'],'--pattern','CC2-Camera-R2-Sources.zip','--dir',str(out)],check=True)
for name in ('RELEASE-NOTES.md','UPDATE_CC2.md','UPDATE_CC2.it.md','SHA256SUMS.txt'):
 (out/name).write_bytes((root/name).read_bytes())
subprocess.run(['node',str(root/'verify.mjs'),str(out),str(root/'publication.json')],check=True)
subprocess.run(['gh','release','create',data['tag'],'--target',data['target'],'--title',data['title'],'--notes-file',str(out/'RELEASE-NOTES.md'),'--draft','--prerelease',*[str(out/n) for n in data['files']]],check=True)
release=next(r for r in api('releases?per_page=100') if r['tag_name']==data['tag'])
assert release['draft'] and release['prerelease'] and len(release['assets'])==len(data['files'])
for asset in release['assets']:
 assert asset['name'] in data['files']
 assert asset['digest']=='sha256:'+data['files'][asset['name']]
 assert asset['size']==(out/asset['name']).stat().st_size
subprocess.run(['gh','release','edit',data['tag'],'--draft=false'],check=True)
release=api('releases/'+str(release['id']))
assert release['prerelease'] and not release['draft']
assert api('releases/latest')['tag_name']==data['stable_tag']
print('Published verified experimental prerelease; stable latest unchanged:',data['tag'])
