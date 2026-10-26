import argparse,hashlib,json,re,subprocess,zipfile
from pathlib import Path
root=Path(__file__).resolve().parents[2]
parser=argparse.ArgumentParser();parser.add_argument('--spanish',action='store_true');args=parser.parse_args()
if args.spanish:
    p=root/'cc2-control/web-src/public/locales/es.json'
    d=json.loads(p.read_text())
    for key in ('updates.status','updates.selected'):
        d[key]=d[key].rstrip(':')+':'
    p.write_text(json.dumps(d,ensure_ascii=False,indent=2)+'\n')
else:
    commit=subprocess.check_output(['git','rev-parse','HEAD'],cwd=root,text=True).strip()
    paths=subprocess.check_output(['git','ls-files','-z','cc2-control'],cwd=root).decode().split('\0')
    archive=root/'builder/current/components/cc2-control/source/source.zip'
    with zipfile.ZipFile(archive,'w',zipfile.ZIP_DEFLATED,compresslevel=9) as out:
        for name in sorted(filter(None,paths)):
            p=root/name
            if not p.is_file(): continue
            info=zipfile.ZipInfo(p.relative_to(root/'cc2-control').as_posix(),(2026,1,1,0,0,0))
            info.compress_type=zipfile.ZIP_DEFLATED
            info.external_attr=(0o100755 if p.suffix in ('.sh','.init') else 0o100644)<<16
            out.writestr(info,p.read_bytes())
    digest=hashlib.sha256(archive.read_bytes()).hexdigest()
    for name,prefix in [('prepare.py','SOURCE'),('core/firmware_builder.py','CC2_CONTROL_SOURCE')]:
        p=root/'builder/current'/name;s=p.read_text()
        for suffix,value in [('COMMIT',commit),('SHA256',digest)]:
            s,n=re.subn(r"(?m)^"+prefix+'_'+suffix+r"\s*=\s*['\"][^'\"]+['\"]",prefix+'_'+suffix+"='"+value+"'",s)
            assert n==1,(name,suffix,n)
        p.write_text(s)
    digest=hashlib.sha256((root/'builder/current/core/firmware_builder.py').read_bytes()).hexdigest()
    p=root/'builder/current/launch_helpers/run_build.py';s=p.read_text()
    s,n=re.subn(r"(?m)^BUILDER_SHA256\s*=\s*['\"][^'\"]+['\"]","BUILDER_SHA256='"+digest+"'",s);assert n==1;p.write_text(s)
