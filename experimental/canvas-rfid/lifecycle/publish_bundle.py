"""Generate a closed publisher file table and deterministic private runtime.

Input artifact qualification belongs to the calling packager. This module only
assembles bytes and never opens printer paths or executes a firmware program.
"""
import gzip
import hashlib
import io
import json
from pathlib import PurePosixPath
import tarfile

sha=lambda data:hashlib.sha256(data).hexdigest()

def configuration(files,modes,before,app_binding=False):
    if not files or len(files)>40 or set(files)!=set(modes):
        raise ValueError('Incomplete bounded publisher configuration')
    nested=(
        {'incoming/'+name for name in ('coordinator','capture','app-integration.json',
                                      'activate-on-ready','transfer-worker')}
        if app_binding else {f'payload/{i}' for i in range(8)}
    )
    rows=[]
    for name,data in sorted(files.items()):
        path=PurePosixPath(name)
        if (str(path)!=name or path.is_absolute() or '..' in path.parts or
            (len(path.parts)!=1 and name not in nested) or
            modes[name] not in [0o600,0o700] or len(data)>4*1024*1024):
            raise ValueError(f'Unsafe component publisher entry {name!r}: {len(data)} bytes, mode {modes[name]:#o}')
        rows.append('{%s,%du,0%o,%s},'%(json.dumps(name),len(data),modes[name],json.dumps(sha(data))))
    dependencies=[]
    for path,digest,mode in before:
        if not path.startswith('/') or '..' in PurePosixPath(path).parts or len(digest)!=64 or any(c not in '0123456789abcdef' for c in digest):
            raise ValueError('Invalid closed native dependency')
        dependencies.append('{%s,SIZE_MAX,0%o,%s},'%(json.dumps(path),mode,json.dumps(digest)))
    if not dependencies:raise ValueError('Missing exact previous profile')
    return ('static const struct cp_file cp_files[]={\n'+'\n'.join(rows)+'\n};\n'+
            'static const struct cp_file cp_before[]={\n'+'\n'.join(dependencies)+'\n};\n').encode()

def runtime(files,modes,publisher):
    if publisher[:6]!=b'\x7fELF\x01\x01' or publisher[18:20]!=b'\x28\x00':
        raise ValueError('Production publisher must be ARM32 little-endian ELF')
    entries={'component/'+n:(data,modes[n]) for n,data in files.items()}
    entries['publisher']=(publisher,0o700)
    manifest=''.join(f'{sha(data)}  {name}\n' for name,(data,_) in sorted(entries.items())).encode()
    entries['SHA256SUMS']=(manifest,0o600)
    buffer=io.BytesIO()
    with gzip.GzipFile(fileobj=buffer,mode='wb',mtime=0) as compressed:
        with tarfile.open(fileobj=compressed,mode='w',format=tarfile.USTAR_FORMAT) as archive:
            for name,(data,mode) in sorted(entries.items()):
                item=tarfile.TarInfo(name);item.size=len(data);item.mode=mode;item.uid=item.gid=0;item.mtime=0
                archive.addfile(item,io.BytesIO(data))
    return buffer.getvalue()
