"""Publish only checksum-pinned CI artifacts with an already-created public signature.
No private signing material is needed or read by this workflow.
"""
from pathlib import Path
import base64,gzip,hashlib,json,subprocess,zipfile
root=Path(__file__).parent
data=json.loads((root/'publication.json').read_text())
out=Path('release-publication-output');out.mkdir()
ci=out/'ci';ci.mkdir()
subprocess.run(['gh','run','download',str(data['ci_run']),'-n',data['artifact_name'],'-D',str(ci)],check=True)
name='CC2-Control-1.2.19-Multiplatform-Update.zip'
installer=(ci/name).read_bytes()
assert hashlib.sha256(installer).hexdigest()==data['files'][name]
(out/name).write_bytes(installer)
with zipfile.ZipFile(ci/name) as z:
 prefix=name[:-4]+'/'
 source=z.read(prefix+'cc2-control-sources.zip')
 payload=gzip.decompress(z.read(prefix+'cc2-control-payload.tar.gz'))
assert len(payload)<=32*1024*1024 and len(payload)%512==0
(out/'CC2-Control-1.2.19-Sources.zip').write_bytes(source)
app=base64.b64decode(data['signed_header'],validate=True)+payload
(out/'CC2-Control-1.2.19-V4.4.cc2update').write_bytes(app)
for name,encoded in data['small_files'].items():
 assert Path(name).name==name
 (out/name).write_bytes(base64.b64decode(encoded,validate=True))
for name,digest in data['files'].items():
 assert hashlib.sha256((out/name).read_bytes()).hexdigest()==digest,name
subprocess.run(['node',str(root/'verify.mjs'),str(out),str(root/'publication.json')],check=True)
checks=json.loads(subprocess.check_output(['gh','api','repos/Neme77/centauri-carbon-2-community/commits/79935e085b78beb089416f32023c03b9c8007a56/check-runs'],text=True))['check_runs']
assert len([c for c in checks if c['conclusion']=='success'])>=8 and all(c['conclusion'] in ('success','skipped') for c in checks)
subprocess.run(['gh','release','create',data['tag'],'--target',data['target'],'--title',data['title'],'--notes-file',str(out/'RELEASE-NOTES.md'),'--draft',*[str(out/n) for n in data['files']]],check=True)
releases=json.loads(subprocess.check_output(['gh','api','repos/Neme77/centauri-carbon-2-community/releases?per_page=100'],text=True))
release=next(r for r in releases if r['tag_name']==data['tag'])
assert release['draft'] and len(release['assets'])==len(data['files'])
for asset in release['assets']:
 assert asset['name'] in data['files']
 assert asset['digest']=='sha256:'+data['files'][asset['name']],asset['name']
 assert asset['size']==(out/asset['name']).stat().st_size
subprocess.run(['gh','release','edit',data['tag'],'--draft=false','--latest'],check=True)
print('Published verified release:',data['tag'])
