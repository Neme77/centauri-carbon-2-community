"""Build the manager and integrated app; no printer or release is modified."""
import hashlib,json,os,shutil,subprocess,sys,zipfile
from pathlib import Path
root=Path(__file__).resolve().parent
os.chdir(root)
out=root/'output';out.mkdir(exist_ok=True)
def run(args,**kw):
    return subprocess.run([str(x) for x in args],check=True,timeout=300,**kw)
flags=['-std=c11','-Os','-Wall','-Wextra','-Wpedantic','-Werror','-static','-s']
helper=out/'control'
run(['arm-linux-gnueabihf-gcc',*flags,'manager.c','-lm','-o',helper])
digest=hashlib.sha256(helper.read_bytes()).hexdigest()
status=run(['sudo','-n','qemu-arm',helper,'--status'],capture_output=True,text=True)
decoded=json.loads(status.stdout)
assert decoded['temporary'] is False and decoded['automatic_selection'] is False
app=out/'cc2-control'
assert not app.exists()
shutil.copytree(root.parents[1]/'cc2-control',app,ignore=shutil.ignore_patterns('node_modules','build','dist'))
run([sys.executable,root.parent/'network-manager/integrate-ui.py',app])
run([sys.executable,'integrate.py',app,digest])
run(['make','-C',app,'build/cc2-control','CROSS=','CC=gcc','EXTRA_CFLAGS=-Werror -DCC2_NETWORK_STATUS_TEST=\\\"/tmp/cc2-network-http-test.sock\\\"'])
run([sys.executable,'test_http.py',app/'build/cc2-control'])
run(['make','-C',app,'clean'])
run(['make','-C',app,'build/cc2-control'])
shutil.copy2(app/'build/cc2-control',out/'cc2-control-network')
env=dict(os.environ,CC2_TEST_RUNNER='qemu-arm')
run([sys.executable,app/'tests/test_smoke.py',out/'cc2-control-network'],env=env)
web=app/'web-src'
run(['npm','ci'],cwd=web)
run(['npm','run','format'],cwd=web)
run(['npm','run','check'],cwd=web)
run(['npm','run','build'],cwd=web)
with zipfile.ZipFile(out/'integrated-sources.zip','w',zipfile.ZIP_DEFLATED) as z:
    for f in sorted(app.rglob('*')):
        rel=f.relative_to(app)
        if f.is_file() and not set(rel.parts)&{'node_modules','build','dist','.git'}:z.write(f,'cc2-control/'+rel.as_posix())
    for f in sorted(root.iterdir()):
        if f.is_file() and f.name!='IMPLEMENTATION.md':z.write(f,'network-connections/'+f.name)
    z.write(root.parents[1]/'LICENSE','LICENSE.GPL-3.0')
report={'manager_compiler':run(['arm-linux-gnueabihf-gcc','--version'],capture_output=True,text=True).stdout.splitlines()[0],
        'manager_static_arm_readonly_qemu':True,'actual_http_guards':True,'app_arm_smoke':True,
        'manual_selection':True,'unconditional_rollback_timer':False,'physical_validation':False,
        'sha256':{f.name:hashlib.sha256(f.read_bytes()).hexdigest() for f in (helper,out/'cc2-control-network',out/'integrated-sources.zip')}}
(out/'validation.json').write_text(json.dumps(report,indent=2)+'\n')
print(json.dumps(report,indent=2))
