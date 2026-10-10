"""Optional, hash-pinned Canvas full-ejection integration. No signing bypass."""
import hashlib
import json
from pathlib import Path
import re
import shutil
import struct
import subprocess

EXTRAS='33b40a530c8c1ffe27e63a7d9f0fd68f3ccfae2cec44edfce10566831b085c41'
GUI='afa2b1f181d18dc4803a60ee61fb3fc454f1e91afa12743bc1a1e5e386e439ff'
NAMES=('libcc2-canvas-eject.so','libcc2-canvas-screen.so')
DEST='/opt/inst/canvas-eject'
def digest(path):return hashlib.sha256(Path(path).read_bytes()).hexdigest()
def load(component):
    component=Path(component);m=json.loads((component/'manifest.json').read_text())
    if m.get('gcc')!='6.5.0' or set(m.get('files',{}))!=set(NAMES):raise RuntimeError('Unqualified Canvas component')
    source=Path(__file__).resolve().parents[3]/'experimental/canvas-eject'
    if set(m.get('sources',{}))!={'runtime.cpp','screen.cpp','eject.h','hook.h','qualified.h'}:raise RuntimeError('Incomplete Canvas provenance')
    for name,value in m['sources'].items():
        if digest(source/name)!=value:raise RuntimeError('Stale Canvas module: '+name)
    for name,value in m['files'].items():
        path=component/name;data=path.read_bytes()
        if digest(path)!=value or data[:6]!=b'\x7fELF\x01\x01' or struct.unpack_from('<H',data,18)[0]!=40:raise RuntimeError('Invalid Canvas ARM module: '+name)
    return component,m
def patched_init(text):
    result,count=re.subn(r'(?m)^([ \t]*)ec-eeb001-gui(?=\s|$)',r'\1/opt/inst/canvas-eject/launch-screen.sh',text)
    if count!=1:raise RuntimeError('Expected exactly one GUI launch line')
    return result
def executable_sections(path):
    data=Path(path).read_bytes()
    off=struct.unpack_from('<I',data,32)[0];size,count,names=struct.unpack_from('<HHH',data,46)
    sections=[struct.unpack_from('<10I',data,off+i*size) for i in range(count)]
    strings=sections[names];strings=data[strings[4]:strings[4]+strings[5]]
    result={}
    for s in sections:
        name=strings[s[0]:].split(b'\0',1)[0]
        if s[2]&4:result[name]=(s[3],data[s[4]:s[4]+s[5]])
        if name==b'.dynsym':
            # Adding DT_NEEDED can rebuild .dynstr and therefore change st_name.
            # Compare resolved names and symbol ABI, not string-table offsets.
            linked=sections[s[6]];symbols=data[linked[4]:linked[4]+linked[5]]
            entries=[]
            for pos in range(s[4],s[4]+s[5],s[9]):
                start,value,length,info,other,index=struct.unpack_from('<IIIBBH',data,pos)
                symbol=symbols[start:symbols.index(b'\0',start)]
                section=index
                if 0<index<len(sections):
                    section=strings[sections[index][0]:].split(b'\0',1)[0]
                entries.append((symbol,value,length,info,other,section))
            result[name]=tuple(sorted(entries,key=repr))
    return result
def install(root,component,patchelf='patchelf'):
    root=Path(root);component,m=load(component)
    extras=root/'opt/lib/libelegoo_extras.so';gui=root/'opt/bin/ec-eeb001-gui'
    if digest(extras)!=EXTRAS or digest(gui)!=GUI:raise RuntimeError('Canvas requires exact 02.01 stock extras and qualified community GUI')
    dest=root/DEST.lstrip('/');dest.mkdir(parents=True,exist_ok=False)
    for name in NAMES:shutil.copyfile(component/name,dest/name);(dest/name).chmod(0o755)
    # A dependency loads the Canvas module in the same printer process as the
    # reactor module, without replacing or weakening either launcher.
    code=executable_sections(extras)
    subprocess.run([patchelf,'--add-needed',DEST+'/'+NAMES[0],str(extras)],check=True)
    if executable_sections(extras)!=code:raise RuntimeError('ELF dependency edit changed vendor code or symbol addresses')
    gui_launcher='''#!/bin/sh
set -eu
BASE=/opt/inst/canvas-eject
fallback() { exec /opt/bin/ec-eeb001-gui "$@"; }
[ ! -e /opt/usr/cc2-canvas-eject-disabled ] || fallback "$@"
[ -z "${LD_PRELOAD:-}" ] || fallback "$@"
[ "$(sha256sum /opt/bin/ec-eeb001-gui | awk '{print $1}')" = "@GUI@" ] || fallback "$@"
[ "$(sha256sum "$BASE/libcc2-canvas-screen.so" | awk '{print $1}')" = "@MODULE@" ] || fallback "$@"
export LD_PRELOAD="$BASE/libcc2-canvas-screen.so"
exec /opt/bin/ec-eeb001-gui "$@"
'''.replace('@GUI@',GUI).replace('@MODULE@',m['files'][NAMES[1]])
    (dest/'launch-screen.sh').write_text(gui_launcher);(dest/'launch-screen.sh').chmod(0o755)
    init=root/'etc/init.d/printer';original=init.read_text();(dest/'printer-init.original.sh').write_text(original)
    init.write_text(patched_init(original))
    record={'extras':digest(extras),'gui':GUI,'files':m['files'],'launcher':digest(dest/'launch-screen.sh'),'init_original':digest(dest/'printer-init.original.sh')}
    (dest/'installed.json').write_text(json.dumps(record,indent=2)+'\n')
    return record
def audit(root,component):
    root=Path(root);_,m=load(component);dest=root/DEST.lstrip('/');record=json.loads((dest/'installed.json').read_text())
    if digest(root/'opt/lib/libelegoo_extras.so')!=record['extras'] or digest(root/'opt/bin/ec-eeb001-gui')!=GUI:raise RuntimeError('Rebuilt Canvas vendor target mismatch')
    if record['files']!=m['files']:raise RuntimeError('Rebuilt Canvas module manifest mismatch')
    for name,value in m['files'].items():
        if digest(dest/name)!=value or (dest/name).stat().st_mode&0o777!=0o755:raise RuntimeError('Rebuilt Canvas module mismatch: '+name)
    if digest(dest/'launch-screen.sh')!=record['launcher'] or digest(dest/'printer-init.original.sh')!=record['init_original']:raise RuntimeError('Rebuilt Canvas launch hook mismatch')
    init=(root/'etc/init.d/printer').read_text()
    if len(re.findall(r'(?m)^[ \t]*'+DEST+r'/launch-screen.sh(?=\s|$)',init))!=1:raise RuntimeError('Rebuilt Canvas screen launch line missing')
