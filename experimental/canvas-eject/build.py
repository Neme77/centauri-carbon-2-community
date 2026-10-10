#!/usr/bin/env python3
"""Build standalone ARM modules; output stays outside the source tree."""
import argparse
import hashlib
import json
from pathlib import Path
import re
import subprocess

HERE=Path(__file__).resolve().parent
SOURCES=('runtime.cpp','screen.cpp','eject.h','hook.h','qualified.h')
def digest(path):return hashlib.sha256(Path(path).read_bytes()).hexdigest()
def main():
    p=argparse.ArgumentParser();p.add_argument('--compiler',required=True);p.add_argument('--output',required=True)
    a=p.parse_args();out=Path(a.output).resolve()
    if HERE==out or HERE in out.parents:raise RuntimeError('Put generated modules outside the source directory')
    out.parent.mkdir(parents=True,exist_ok=True)
    version=subprocess.check_output([a.compiler,'-dumpversion'],text=True).strip()
    if version!='6.5.0':raise RuntimeError('Qualified module build requires GCC 6.5.0')
    subprocess.run(['g++','-std=c++11','-Wall','-Wextra','-Werror',str(HERE/'test_eject.cpp'),'-o',str(out.parent/'eject-host-test')],check=True)
    subprocess.run([str(out.parent/'eject-host-test')],check=True)
    out.mkdir(parents=True,exist_ok=True)
    flags=['-std=c++11','-O0','-g','-Wall','-Wextra','-Werror','-marm','-mcpu=cortex-a7',
        '-mfpu=neon-vfpv4','-mfloat-abi=hard','-fPIC','-fexceptions','-funwind-tables','-shared','-Wl,--no-undefined','-Wl,-z,now']
    files={}
    for source,name in [('runtime.cpp','libcc2-canvas-eject.so'),('screen.cpp','libcc2-canvas-screen.so')]:
        target=out/name
        subprocess.run([a.compiler,*flags,str(HERE/source),'-o',str(target),'-ldl','-pthread'],check=True)
        readelf=a.compiler.rsplit('-',1)[0]+'-readelf'
        versions=subprocess.check_output([readelf,'--version-info',str(target)],text=True)
        for prefix,maximum in [('GLIBC',(2,23)),('GLIBCXX',(3,4,22))]:
            for v in re.findall(r'\b'+prefix+r'_([0-9.]+)',versions):
                if tuple(map(int,v.split('.')))>maximum:raise RuntimeError('Runtime dependency too new: '+prefix+'_'+v)
        files[name]=digest(target)
    manifest={'gcc':version,'sources':{s:digest(HERE/s) for s in SOURCES},'files':files}
    (out/'manifest.json').write_text(json.dumps(manifest,indent=2)+'\n')
    print('PASS ARM modules, qualified compiler and library-version limits:',out)
if __name__=='__main__':main()
