"""Read-only installed-profile snapshot; never executes collected contents.

Vendor binaries, keys, Wi-Fi credentials and G-code are not collected. Scripts,
activation markers and checksums are private migration inputs, not release assets.
"""
import hashlib
import io
import json
from pathlib import Path
import re
import shlex
import tarfile

META = b'CC2_PROFILE_META_V1\n'
ARCHIVE = b'\nCC2_PROFILE_ARCHIVE_V1\n'
MAX_STREAM = 8 * 1024 * 1024
MAX_FILE = 128 * 1024
FILES = (
    'opt/bin/run_printer.sh', 'etc/init.d/cc2-control',
    'opt/inst/cc2-control/launch.sh',
    'etc/init.d/printer', 'etc/init.d/cc2-reactor-launch',
    'etc/init.d/cc2-network-manager', 'etc/rc.d/S23cc2-network-manager',
    'opt/inst/run_printer.sh',
    *('opt/inst/cc2-reactor/'+name for name in (
        'firmware-launcher.sh','launcher.sh','preflight.sh','SHA256SUMS',
        'printer-init.original.sh','run_printer.original.sh')),
    *('opt/usr/cc2-reactor-runtime-v2/'+name for name in (
        'launcher.sh','preflight.sh','SHA256SUMS','firmware-payload.sha256')),
    *('opt/usr/cc2-network-manager/'+name for name in (
        'profile.sh','reactor-entry.sh','activate.sh','SHA256SUMS','startup.sha256')),
    *('opt/usr/cc2-cavity-floor/'+name for name in (
        'enabled','SHA256SUMS','launcher.sh','profile.sh')),
    *(root+'/'+name for root in (
        'opt/usr/cc2-rfid-native-r1','opt/usr/cc2-rfid-native-r2','opt/usr/cc2-rfid-frame-fix-r1')
      for name in ('enabled','SHA256SUMS','backup/plan',
        *('backup/'+role+'.old' for role in ('reactor-launcher','reactor-manifest',
          'network-profile','network-entry','network-activation','network-manifest')))),
    'opt/usr/cc2-canvas-rfid-trial-r1a/recovery-ticket',
    'tmp/cc2-rfid-native-r2/ready',
)
BINARY_PINS = (
    '/opt/bin/elegoo_printer', '/opt/usr/cc2-reactor-runtime-v2/elegoo_printer',
    '/opt/lib/libcolib.so','/opt/usr/cc2-control/cc2-control',
    '/opt/usr/cc2-reactor-runtime-v2/libcc2-reactor-v2.so',
    '/opt/usr/cc2-network-manager/control',
    '/opt/usr/cc2-network-manager/libcc2-network-compat-r1.so',
    '/opt/usr/cc2-cavity-floor/libcc2-cavity-floor-r1.so',
    '/opt/usr/cc2-rfid-native-r1/libcc2-rfid-native-r1.so',
    '/opt/usr/cc2-rfid-native-r2/libcc2-rfid-native-r2.so',
    '/opt/usr/cc2-rfid-frame-fix-r1/libcc2-rfid-native-r2.so',
    '/opt/usr/cc2-rfid-frame-capture-r1/cc2-rfid-frame-capture-r1',
    '/opt/usr/cc2-control/camera/cc2-camera-reap-check',
    '/opt/inst/ota/canvas_gd32-20251029_184624-00.00.00.44-app.bin',
    '/opt/usr/cc2-canvas-rfid-trial-r1a/original-stock-44.bin',
)

def remote_script():
    selected = ' '.join(shlex.quote('/'+name) for name in FILES)
    binaries = ' '.join(shlex.quote(name) for name in BINARY_PINS)
    return f'''#!/bin/sh
set -eu
printf '{META.decode()}'
printf 'KERNEL '; uname -r
printf 'BOOT_ID '; cat /proc/sys/kernel/random/boot_id
for D in /etc/init.d /opt/usr /opt/usr/cc2-reactor-runtime-v2 /opt/usr/cc2-network-manager /opt/usr/cc2-cavity-floor /opt/usr/cc2-rfid-native-r2 /opt/usr/cc2-rfid-frame-fix-r1; do
    ls -ldn "$D" 2>/dev/null || true
done
for F in {binaries}; do
    if [ -f "$F" ] && [ ! -L "$F" ]; then
        ls -ldn "$F"
        printf 'BINARY_SHA '; sha256sum "$F"
    fi
done
for P in $(pidof elegoo_printer 2>/dev/null || true); do
    case "$P" in ''|*[!0-9]*) continue;; esac
    printf 'NATIVE_PID %s\\n' "$P"
    readlink "/proc/$P/exe" || true
    printf 'RUNNING_SHA '; sha256sum "/proc/$P/exe" || true
    grep 'libcc2-' "/proc/$P/maps" | awk '{{print $6}}' | sort -u || true
done
for A in health printer uds canvas; do
    printf 'API /api/%s\\n' "$A"
    wget -T 5 -qO- "http://127.0.0.1:8081/api/$A" 2>/dev/null || printf 'UNAVAILABLE'
    printf '\\n'
done
for F in {selected}; do
    if [ -f "$F" ] && [ ! -L "$F" ]; then
        ls -ldn "$F"
        printf 'FILE_SHA '; sha256sum "$F"
    fi
done
set --
for F in {selected}; do
    if [ -f "$F" ] || [ -L "$F" ]; then set -- "$@" "${{F#/}}"; fi
done
[ "$#" -gt 0 ] || exit 1
printf '\\nCC2_PROFILE_ARCHIVE_V1\\n'
cd /
exec tar -cf - "$@"
'''

def parse_capture(blob):
    if not blob or len(blob)>MAX_STREAM:
        raise ValueError('Snapshot missing or too large')
    start=blob.find(META)
    if start<0 or start>8192 or (start and blob[start-1:start]!=b'\n'):
        raise ValueError('Profile snapshot header missing')
    split=blob.find(ARCHIVE,start+len(META))
    if split<0 or split-start>256*1024:
        raise ValueError('Bounded metadata/archive separator missing')
    metadata=blob[start+len(META):split].decode('utf-8',errors='strict')
    checksums={}
    for line in metadata.splitlines():
        if not line.startswith('FILE_SHA '):continue
        match=re.fullmatch(r'FILE_SHA ([0-9a-f]{64})  /([^\r\n]+)',line)
        if not match or match[2] not in FILES or match[2] in checksums:
            raise ValueError('Unexpected or duplicate file checksum')
        checksums[match[2]]=match[1]
    records=[];content={};seen=set()
    with tarfile.open(fileobj=io.BytesIO(blob[split+len(ARCHIVE):]),mode='r:') as archive:
        for member in archive:
            if member.name not in FILES or member.name in seen or len(seen)>=len(FILES):
                raise ValueError('Unexpected or duplicate archive entry')
            seen.add(member.name)
            record={'path':'/'+member.name,'mode':member.mode,'uid':member.uid,'gid':member.gid,
                    'size':member.size,'mtime':member.mtime}
            if member.isreg():
                if not 0<=member.size<=MAX_FILE:
                    raise ValueError('Snapshot member exceeds bound')
                data=archive.extractfile(member).read(MAX_FILE+1)
                digest=hashlib.sha256(data).hexdigest()
                if len(data)!=member.size or checksums.get(member.name)!=digest:
                    raise ValueError('File changed during collection or checksum missing')
                identifier=hashlib.sha256(member.name.encode()).hexdigest()[:24]+'.bin'
                record.update(type='regular',sha256=digest,local_name=identifier)
                content[identifier]=data
            elif member.issym() or member.islnk():
                # Store link metadata only. Never follow/extract a captured link.
                record.update(type='symlink' if member.issym() else 'hardlink',link=member.linkname)
            else:
                raise ValueError('Special files are not permitted in a profile snapshot')
            records.append(record)
    if not records or set(checksums)-seen:
        raise ValueError('Incomplete snapshot')
    return {'format':1,'stream_sha256':hashlib.sha256(blob).hexdigest(),'metadata':metadata,
            'files':records,'printer_modified':False,'private_migration_input':True},content

def save_capture(path,output):
    report,content=parse_capture(Path(path).read_bytes())
    output=Path(output);output.mkdir(mode=0o700,parents=True,exist_ok=False)
    for name,data in content.items():
        target=output/name;target.write_bytes(data);target.chmod(0o600)
    (output/'inspection.json').write_text(json.dumps(report,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
    return report
