"""Deliver the already qualified chamber component through the app installer."""
import hashlib
import io
import tarfile

BEGIN='# CC2_CAVITY_DATA_BEGIN'
END='# CC2_CAVITY_DATA_END'
NAMES={'SHA256SUMS','activate.sh','launcher.sh','libcc2-cavity-floor-r1.so','manage.sh',
       'profile.original.sha256','profile.sh','reactor-entry.sh','thermal.awk','installer-barrier'}


def functions(runtime):
    data=runtime.read_bytes()
    if not data or len(data)>4*1024*1024:raise ValueError('Unbounded cavity runtime')
    files={};total=0
    with tarfile.open(fileobj=io.BytesIO(data),mode='r:gz') as archive:
        for item in archive:
            total+=item.size
            if (not item.isfile() or item.name in files or item.name not in
                ({'publisher','SHA256SUMS'}|{'component/'+n for n in NAMES}) or
                item.size<1 or item.size>1024*1024 or total>4*1024*1024 or item.uid or item.gid or
                item.mode not in [0o600,0o700]):raise ValueError('Unsafe cavity member')
            files[item.name]=archive.extractfile(item).read()
    if set(files)!={'publisher','SHA256SUMS'}|{'component/'+n for n in NAMES}:
        raise ValueError('Incomplete cavity runtime')
    def manifest(data):
        rows={}
        for line in data.decode('ascii').splitlines():
            h,n=line.split('  ',1)
            if len(h)!=64 or any(c not in '0123456789abcdef' for c in h) or n in rows:raise ValueError('Invalid cavity manifest')
            rows[n]=h
        return rows
    digest=lambda data:hashlib.sha256(data).hexdigest()
    outer=manifest(files['SHA256SUMS']);inner=manifest(files['component/SHA256SUMS'])
    if set(outer)!=set(files)-{'SHA256SUMS'} or any(digest(files[n])!=h for n,h in outer.items()):
        raise ValueError('Cavity runtime mismatch')
    if set(inner)!=NAMES-{'SHA256SUMS'} or any(digest(files['component/'+n])!=h for n,h in inner.items()):
        raise ValueError('Cavity component mismatch')
    for name in ['publisher','component/installer-barrier']:
        elf=files[name]
        if len(elf)<20 or elf[:6]!=b'\x7fELF\x01\x01' or elf[18:20]!=b'\x28\x00':raise ValueError('Cavity helper must be ARM32 ELF')
    if ('INSTALLER_GUARD_SHA='+inner['installer-barrier']+'\n').encode() not in files['component/manage.sh']:
        raise ValueError('Cavity manager must pin its installer barrier helper')
    # Keep the physically qualified library byte-identical.
    if inner['libcc2-cavity-floor-r1.so']!='52d400605dd51f89eb95248c42a1661b3c0e60fadec5453d27a987729332fb10':
        raise ValueError('Unqualified cavity library')
    code=f'''CAVITY_SOURCE=''
CAVITY_ROOT=/opt/usr/cc2-cavity-floor
cavity_unpack() {{
    CAVITY_SOURCE="$STAGE/cavity-runtime-$$"
    (umask 077; mkdir "$CAVITY_SOURCE") || return 1
    sed -n '/^{BEGIN}$/,/^{END}$/p' "$STAGE/install-on-printer.sh" | sed '/^#/d' | sh > "$CAVITY_SOURCE/runtime.tar.gz"
    [ "$(sha256sum "$CAVITY_SOURCE/runtime.tar.gz" | awk '{{print $1}}')" = {digest(data)} ] || return 1
    tar -xzf "$CAVITY_SOURCE/runtime.tar.gz" -C "$CAVITY_SOURCE" || return 1
    (cd "$CAVITY_SOURCE" && sha256sum -c SHA256SUMS) || return 1
    chmod 700 "$CAVITY_SOURCE/publisher" || return 1
    if [ -e "$CAVITY_ROOT" ] || [ -L "$CAVITY_ROOT" ]; then
        LC_ALL=C ls -ldn "$CAVITY_ROOT" | awk 'NR==1{{ok=($1=="drwx------"&&$3==0&&$4==0)}}END{{exit !(NR==1&&ok)}}' || return 1
        [ -d "$CAVITY_ROOT" ] && [ ! -L "$CAVITY_ROOT" ] &&
        [ ! -L "$CAVITY_ROOT/SHA256SUMS" ] &&
        [ "$(sha256sum "$CAVITY_ROOT/SHA256SUMS" | awk '{{print $1}}')" = {outer['component/SHA256SUMS']} ] || return 1
        for name in {' '.join(sorted(NAMES))}; do
            [ -f "$CAVITY_ROOT/$name" ] && [ ! -L "$CAVITY_ROOT/$name" ] || return 1
            LC_ALL=C ls -ldn "$CAVITY_ROOT/$name" | awk 'NR==1{{ok=($1~/^-/&&$2==1&&$3==0)}}END{{exit !(NR==1&&ok)}}' || return 1
        done
        (cd "$CAVITY_ROOT" && sha256sum -c SHA256SUMS) || return 1
    else
        "$CAVITY_SOURCE/publisher" --publish "$CAVITY_SOURCE/component" || return 1
    fi
    exec 3< "$LOCK"
    cavity_run check-from-installer
}}
cavity_run() {{
    cavity_attempt=1
    while :;do
        cavity_log="$CAVITY_SOURCE/check-$cavity_attempt.log"
        if sh "$CAVITY_ROOT/manage.sh" "$1" 3 >"$cavity_log" 2>&1;then cat "$cavity_log";return 0;else cavity_code=$?;fi
        cat "$cavity_log"
        [ "$cavity_attempt" -lt 3 ] && grep -qx 'STOP: stale_telemetry' "$cavity_log" &&
        [ "$(grep -c '^STOP:' "$cavity_log")" = 1 ] || return "$cavity_code"
        sleep 2;cavity_attempt=$((cavity_attempt+1))
    done
}}
cavity_prepare() {{
    cavity_run enable-from-installer
}}
'''
    tail='\n'+BEGIN+'\n'
    for start in range(0,len(data),4096):tail+="printf '"+''.join('\\%03o'%b for b in data[start:start+4096])+"' || exit 1\n"
    return code,tail+END+'\n'
