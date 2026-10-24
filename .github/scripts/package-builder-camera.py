"""Assemble a public source builder only; never create or publish a firmware image."""
from pathlib import Path
import argparse,hashlib,importlib.util,io,json,subprocess,tarfile,urllib.request,zipfile

ROOT=Path(__file__).resolve().parents[2]
spec=importlib.util.spec_from_file_location('camera_pins',ROOT/'cc2-control/installer/camera_component.py')
camera=importlib.util.module_from_spec(spec);spec.loader.exec_module(camera)
COMPONENT=ROOT/'builder/current/components/camera/runtime'

def stage():
    COMPONENT.mkdir(parents=True,exist_ok=True)
    for name,pin in camera.PINS.items():
        source=Path('/tmp/qualified-recovery' if name=='libcc2-camera-recovery-r2.so' else '/tmp/qualified-reap')
        found=list(source.rglob(name))
        assert len(found)==1,(name,len(found))
        data=found[0].read_bytes();assert hashlib.sha256(data).hexdigest()==pin
        (COMPONENT/name).write_bytes(data)
    print('Unchanged qualified camera libraries staged')

def package():
    name='CC2-Builder-V4.4.2-Control-1.2.21-Test-R4'
    assert (ROOT/'builder/current/RELEASE_VERSION').read_text().strip()=='4.4.2'
    assert (ROOT/'cc2-control/VERSION').read_text().strip()=='1.2.21'
    commit=subprocess.check_output(['git','rev-parse','HEAD'],cwd=ROOT,text=True).strip()
    archive=subprocess.check_output(['git','archive','--format=tar',commit],cwd=ROOT)
    files={}
    with tarfile.open(fileobj=io.BytesIO(archive)) as source:
        for item in source.getmembers():
            if item.isfile():files[item.name]=(source.extractfile(item).read(),item.mode)
    assert not any(('private' in p.lower() and p.endswith('.pem')) or '/original_firmware/' in p or p.endswith('.zip.sig') for p in files)
    for filename,pin in camera.PINS.items():
        data=(COMPONENT/filename).read_bytes();assert hashlib.sha256(data).hexdigest()==pin
        files['builder/current/components/camera/runtime/'+filename]=(data,0o700 if filename=='cc2-camera-reap-check' else 0o600)
    url='https://github.com/Neme77/centauri-carbon-2-community/releases/download/CC2-Control-1.2.19-V4.4/CC2-Camera-R2-Sources.zip'
    with urllib.request.urlopen(url,timeout=60) as response:camera_sources=response.read(1024*1024)
    assert hashlib.sha256(camera_sources).hexdigest()=='a9f734c143f4ae2a12ac1243130b4351408dea31e142b5b039b43f37469c04a4'
    files['sources/CC2-Camera-R2-Sources.zip']=(camera_sources,0o644)
    notes=('BUILDER FIRMWARE COMPLETO V4.4.2 / CC2 CONTROL 1.2.21\n\n'
           'Il firmware va generato sul tuo PC. Avvia builder\\current\\Build-Release.cmd.\n'
           'Indica builder\\current del builder precedente per importare gli input locali.\n'
           'Base ufficiale richiesta: 02.01.01.07. Usa lo stesso ambiente WSL delle build precedenti.\n'
           'Fix webcam R2 integrato; nessun firmware o chiave privata inclusi.\n'
           'Questo pacchetto e una prova: verifica il firmware sulla stampante prima di pubblicarlo.\n'
           'Leggi builder/current/README.md per dettagli, controlli e ripristino.\n')
    files['LEGGIMI-PRIMA.txt']=(notes.encode(),0o644)
    report={'source_commit':commit,'firmware':'4.4.2','control':'1.2.21','compatibility_family':'4.4','official_base':'02.01.01.07',
            'camera_pins':camera.PINS,'firmware_generated':False,'new_complete_firmware_hardware_validated':False}
    files['builder-validation.json']=((json.dumps(report,indent=2)+'\n').encode(),0o644)
    sums=''.join(hashlib.sha256(data).hexdigest()+'  '+p+'\n' for p,(data,_) in sorted(files.items()))
    files['SHA256SUMS.txt']=(sums.encode(),0o644)
    output=ROOT/'output';output.mkdir(exist_ok=True)
    target=output/(name+'.zip')
    with zipfile.ZipFile(target,'w',zipfile.ZIP_DEFLATED,compresslevel=9) as z:
        for p,(data,mode) in sorted(files.items()):
            info=zipfile.ZipInfo(name+'/'+p,(2026,1,1,0,0,0));info.compress_type=zipfile.ZIP_DEFLATED;info.external_attr=(0o100000|mode)<<16
            z.writestr(info,data)
    digest=hashlib.sha256(target.read_bytes()).hexdigest()
    Path(str(target)+'.sha256').write_text(digest+'  '+target.name+'\n')
    print('BUILDER PACKAGE PASS:',target.name,digest,'No firmware generated.')

if __name__=='__main__':
    parser=argparse.ArgumentParser();parser.add_argument('--stage-runtime',action='store_true');parser.add_argument('--package',action='store_true');args=parser.parse_args()
    if args.stage_runtime:stage()
    if args.package:package()
