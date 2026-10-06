#!/usr/bin/env python3
"""Build the pinned combined test on the owner's qualified WSL toolchain."""
import gzip
import hashlib
import io
import json
from pathlib import Path
import re
import shutil
import struct
import subprocess
import tarfile
import zipfile

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[1]
CC2 = ROOT / 'cc2-control'
SOURCE_COMMIT = '24cf5222301bc1657db2153e637b52217380f509'
PREFIX = Path('/opt/cc2-cross/toolchain-out/bin/arm-cortex_a15-linux-gnueabihf')
def run(args, **kw):
    return subprocess.run([str(a) for a in args], check=True, **kw)
def output(args):
    return run(args, stdout=subprocess.PIPE, text=True).stdout
def digest(data):
    return hashlib.sha256(data).hexdigest()
def elf(path):
    data = path.read_bytes()
    if len(data)<20 or data[:6]!=b'\x7fELF\x01\x01' or struct.unpack_from('<H',data,18)[0]!=40:
        raise RuntimeError('Not a 32-bit little-endian ARM ELF: '+str(path))
    return data
def source_zip():
    sources={}
    for directory in (CC2,HERE):
        for path in sorted(directory.rglob('*')):
            relative=path.relative_to(directory)
            if not path.is_file() or any(part in ('build','dist','node_modules','__pycache__','vendor-link') for part in relative.parts):continue
            if path.suffix in ('.so','.pyc') or path.name in ('cc2-reactor-bridge-test','symbols-debug.txt'):continue
            sources[path.relative_to(ROOT).as_posix()]=path.read_bytes()
    for name in ('LICENSE','NOTICE.md','CHANGELOG.md','AGENTS.md','docs/SOURCE.md'):
        if (ROOT/name).is_file():sources[name]=(ROOT/name).read_bytes()
    result=io.BytesIO()
    with zipfile.ZipFile(result,'w',zipfile.ZIP_DEFLATED) as archive:
        for name,data in sorted(sources.items()):
            entry=zipfile.ZipInfo(name,(2026,10,6,0,0,0));entry.compress_type=zipfile.ZIP_DEFLATED
            archive.writestr(entry,data)
    return result.getvalue()
def main():
    run(['python3',HERE/'test_installer.py'])
    run(['python3',HERE/'test_package.py'])
    if output([str(PREFIX)+'-g++','-dumpversion']).strip()!='6.5.0':
        raise RuntimeError('Use the qualified GCC 6.5.0 toolchain')
    libc=Path(output([str(PREFIX)+'-g++','-print-file-name=libc.so.6']).strip())
    if 'release version 2.23' not in output(['strings',libc]):
        raise RuntimeError('Toolchain libc must be glibc 2.23')
    run(['make','clean','test','CROSS=','CC=gcc'],cwd=CC2)
    run(['make','clean','all','CROSS=', 'CC='+str(PREFIX)+'-gcc'],cwd=CC2)
    run(['make','uds-probe','CROSS=', 'CC='+str(PREFIX)+'-gcc'],cwd=CC2)
    reactor=HERE/'reactor'
    flags=['-std=c++11','-O0','-g','-Wall','-Wextra','-mcpu=cortex-a7','-mfpu=neon-vfpv4',
           '-mfloat-abi=hard','-marm','-pthread','-fno-strict-aliasing','-fno-omit-frame-pointer',
           '-fexceptions','-funwind-tables']
    module=reactor/'libcc2-reactor-v2.so'
    bridge=reactor/'cc2-reactor-bridge-test'
    run([str(PREFIX)+'-g++',*flags,'-fPIC','-fvisibility=hidden','-shared','-Wl,--no-undefined',
         '-Wl,-z,now',reactor/'reactor_module.cpp','-o',module,'-ldl','-lm'])
    run([str(PREFIX)+'-g++',*flags,'-DCC2_NATIVE_CO','-I'+str(reactor),reactor/'bridge_test.cpp',
         '-o',bridge,'-L'+str(reactor/'vendor-link'),'-Wl,-rpath,/opt/lib','-lcolib','-ldl','-lm'])
    symbols=output([str(PREFIX)+'-readelf','--wide','--symbols',module])
    (reactor/'symbols-debug.txt').write_text(symbols)
    names={line.split()[-1] for line in symbols.splitlines() if line.split()}
    for name in ('cc2_callback','cc2_timers','cc2_poll','cc2_wait'):
        if name not in names: raise RuntimeError('Missing symbol: '+name)
    if '.ARM.exidx' not in output([str(PREFIX)+'-readelf','-SW',module]):
        raise RuntimeError('Missing exception unwind tables')
    for path in (module,bridge):
        elf(path)
        version=output([str(PREFIX)+'-readelf','--version-info',path])
        for name,maximum in [('GLIBC',(2,23)),('GLIBCXX',(3,4,22))]:
            for value in re.findall(r'\b'+name+r'_([0-9.]+)',version):
                if tuple(map(int,value.split('.')))>maximum:
                    raise RuntimeError('Runtime dependency too new: '+name+'_'+value)
    files={'cc2-control':elf(CC2/'dist/cc2-control/cc2-control'),
           'cc2-uds-probe':elf(CC2/'build/cc2-uds-probe')}
    for path in sorted((CC2/'web').rglob('*')):
        if path.is_file(): files[path.relative_to(CC2).as_posix()]=path.read_bytes()
    for name in ('start.sh','launch.sh','cc2-control.init'):
        files[name]=(CC2/'scripts'/name).read_text(encoding='utf-8-sig').replace('\r\n','\n').replace('\r','\n').encode()
    files['reactor/libcc2-reactor-v2.so']=module.read_bytes()
    files['reactor/cc2-reactor-bridge-test']=bridge.read_bytes()
    for name in ('launcher.sh','preflight.sh'):
        files['reactor/'+name]=(HERE/name).read_bytes()
    files['reactor/SHA256SUMS']=''.join(digest(data)+'  '+name[8:]+'\n'
        for name,data in sorted(files.items()) if name.startswith('reactor/')).encode()
    binary_hash=digest(files['cc2-control'])
    for name in ('install-combined.sh','restore-combined.sh','common.sh','boot-launcher.sh'):
        files[name]=(HERE/name).read_text().replace('@CC2_SHA256@',binary_hash).encode()
    source=source_zip()
    files['build-info.json']=(json.dumps({'version':'1.1.31','test':'combined-uds-reactor-o0',
        'base_commit':SOURCE_COMMIT,'cc2_sha256':binary_hash,'reactor_sha256':digest(module.read_bytes()),
        'source_archive_sha256':digest(source),
        'reactor_optimization':'O0','changes':['PR88 reduced socket polling',
        'callback, timer, poll and exception cleanup bridge'],'hardware_validation':'experimental; long print pending'},indent=2)+'\n').encode()
    files['SHA256SUMS']=''.join(digest(data)+'  '+name+'\n' for name,data in sorted(files.items())).encode()
    payload=io.BytesIO()
    with gzip.GzipFile(fileobj=payload,mode='wb',mtime=0) as zipped:
        with tarfile.open(fileobj=zipped,mode='w') as archive:
            for name,data in sorted(files.items()):
                info=tarfile.TarInfo(name);info.size=len(data);info.uid=info.gid=0;info.mtime=0
                info.mode=0o700 if name.startswith('reactor/') else 0o755 if name.endswith(('.sh','.init')) or name in ('cc2-control','cc2-uds-probe') else 0o644
                archive.addfile(info,io.BytesIO(data))
    public={'combined-payload.tar.gz':payload.getvalue(),'combined-sources.zip':source}
    for name in ('Install-Combined.ps1','Install-Windows.cmd','Restore-Originale.ps1','LEGGIMI.txt'):
        public[name]=(HERE/name).read_bytes()
    public['SHA256SUMS']=''.join(digest(data)+'  '+name+'\n' for name,data in sorted(public.items())).encode()
    out=ROOT/'output/CC2-Control-1.1.31-UDS88-Reactor-O0-Test.zip';out.parent.mkdir(exist_ok=True)
    with zipfile.ZipFile(out,'w',zipfile.ZIP_DEFLATED) as archive:
        for name,data in sorted(public.items()):archive.writestr(out.stem+'/'+name,data)
    print('BUILD PASS. Installer completo:',out)
    print('SHA256:',digest(out.read_bytes()))
    print('Condividi questo ZIP generato, non lo ZIP di build. Installare solo inattiva e fredda.')
if __name__=='__main__': main()
