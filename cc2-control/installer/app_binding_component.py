"""Private same-profile app upgrade; no native library/startup/Canvas delivery."""
import hashlib
import io
import json
import tarfile
from app_binding_bundle import hex64, unique_pairs

BEGIN = '# CC2_APP_BINDING_DATA_BEGIN'
END = '# CC2_APP_BINDING_DATA_END'
NAMES = ('coordinator', 'capture', 'app-integration.json', 'activate-on-ready')


def functions(runtime, app_sha, defer_publication=False):
    data = runtime.read_bytes()
    if not data or len(data) > 4 * 1024 * 1024:
        raise ValueError('Unbounded app binding bundle')
    files = {}
    total = 0
    required = {'publisher', 'component/worker', 'component/recovery.json', 'SHA256SUMS'} | {'component/incoming/' + n for n in NAMES}
    with tarfile.open(fileobj=io.BytesIO(data), mode='r:gz') as archive:
        for entry in archive:
            total += entry.size
            if (not entry.isfile() or entry.name not in required or entry.name in files or
                    entry.size < 1 or entry.size > 1024 * 1024 or total > 4 * 1024 * 1024 or
                    entry.uid or entry.gid or entry.mode != (0o700 if entry.name in
                    ('component/worker', 'publisher', 'component/incoming/coordinator', 'component/incoming/capture') else 0o600)):
                raise ValueError('Unsafe app binding member')
            files[entry.name] = archive.extractfile(entry).read()
    if set(files) != required:
        raise ValueError('Incomplete app binding bundle')
    manifest = {}
    for row in files['SHA256SUMS'].decode('ascii').splitlines():
        digest, name = row.split('  ', 1)
        if not hex64(digest) or name in manifest:
            raise ValueError('Invalid app binding manifest')
        manifest[name] = digest
    if set(manifest) != required - {'SHA256SUMS'} or any(
            hashlib.sha256(files[n]).hexdigest() != h for n, h in manifest.items()):
        raise ValueError('App binding checksum mismatch')
    for name in ('component/worker', 'publisher'):
        elf = files[name]
        if len(elf) < 20 or elf[:6] != b'\x7fELF\x01\x01' or elf[18:20] != b'\x28\x00':
            raise ValueError('App binding executables must be ARM32 ELF')
    metadata = json.loads(files['component/recovery.json'], object_pairs_hook=unique_pairs)
    if (set(metadata) != {'protocol', 'app_sha256', 'worker_sha256', 'plan_sha256'} or
            type(metadata['protocol']) is not int or metadata['protocol'] != 1 or
            metadata['app_sha256'] != app_sha or not hex64(app_sha) or
            metadata['worker_sha256'] != manifest['component/worker'] or not hex64(metadata['plan_sha256'])):
        raise ValueError('App binding descriptor mismatch')
    destination = '/opt/usr/cc2-rfid-app-updates/' + app_sha
    worker = manifest['component/worker']
    descriptor = manifest['component/recovery.json']
    # This fixed authenticated path is also saved in the app backup's restore script.
    restore_functions = f'''binding_verify_worker() {{
    [ ! -L {destination} ] && [ ! -L {destination}/worker ] &&
    [ "$(sha256sum {destination}/worker | awk '{{print $1}}')" = {worker} ] &&
    [ "$(sha256sum {destination}/recovery.json | awk '{{print $1}}')" = {descriptor} ]
}}
binding_rollback() {{
    binding_verify_worker && {destination}/worker --rollback 3
}}
'''
    code = restore_functions + f'''BINDING_PREPARED=0
BINDING_OBSERVATION=0
binding_rejected() {{
    command -v installer_http_get >/dev/null 2>&1 || return 0
    echo 'Latest read-only observations after the refused check (not the rejected historical samples):'
    for binding_endpoint in /api/printer /api/uds;do
        echo "$binding_endpoint"
        installer_http_get "http://127.0.0.1:8081$binding_endpoint" || true
        echo
    done
}}
binding_run() {{
    BINDING_OBSERVATION=$((BINDING_OBSERVATION+1))
    binding_attempt=1
    while :;do
        binding_log="$BINDING_SOURCE/observation-$BINDING_OBSERVATION-$binding_attempt.log"
        if "$@" >"$binding_log" 2>&1;then cat "$binding_log";return 0;else binding_code=$?;fi
        cat "$binding_log"
        [ "$binding_attempt" -lt 3 ] &&
        grep -qx 'STOP: fresh_idle_cold_observation_failed' "$binding_log" &&
        ! grep -Eq '^STOP: app_binding_(unsafe_live_observation|observation_unavailable|canvas_observation_refused)$' "$binding_log" || {{ binding_rejected;return "$binding_code"; }}
        echo 'Waiting briefly for a valid fresh Idle/cold observation; all original checks remain active.'
        sleep 2
        [ -f /opt/usr/cc2-rfid/fresh-reader ] && [ ! -L /opt/usr/cc2-rfid/fresh-reader ] &&
        [ "$(sha256sum /opt/usr/cc2-rfid/fresh-reader | awk '{{print $1}}')" = 3dd6d8699169c800689e5ed2c047a2d3cc43386d3fdad59adc41b929111b045a ] &&
        /opt/usr/cc2-rfid/fresh-reader --check || {{ binding_rejected;return "$binding_code"; }}
        binding_attempt=$((binding_attempt+1))
    done
}}
binding_unpack() {{
    BINDING_SOURCE="$STAGE/app-binding-$$"
    (umask 077; mkdir "$BINDING_SOURCE") || return 1
    sed -n '/^{BEGIN}$/,/^{END}$/p' "$STAGE/install-on-printer.sh" | sed '/^#/d' | sh > "$BINDING_SOURCE/runtime.tar.gz"
    [ "$(sha256sum "$BINDING_SOURCE/runtime.tar.gz" | awk '{{print $1}}')" = {hashlib.sha256(data).hexdigest()} ] || return 1
    tar -xzf "$BINDING_SOURCE/runtime.tar.gz" -C "$BINDING_SOURCE" || return 1
    (cd "$BINDING_SOURCE" && sha256sum -c SHA256SUMS) || return 1
    chmod 700 "$BINDING_SOURCE/publisher" "$BINDING_SOURCE/component" "$BINDING_SOURCE/component/worker" "$BINDING_SOURCE/component/incoming" "$BINDING_SOURCE/component/incoming/coordinator" "$BINDING_SOURCE/component/incoming/capture" || return 1
    exec 3< "$LOCK"
    {' :' if defer_publication else 'binding_publish'}
}}
binding_publish() {{
    if [ -e {destination} ] || [ -L {destination} ]; then
        if ! binding_verify_worker;then
            [ -f {destination}/worker ] && [ ! -L {destination}/worker ] &&
            [ -f {destination}/recovery.json ] && [ ! -L {destination}/recovery.json ] || return 1
            "$BINDING_SOURCE/component/worker" --retire-inactive 3 || return 1
        fi
    fi
    if [ ! -e {destination} ] && [ ! -L {destination} ];then
        if [ ! -e /opt/usr/cc2-rfid-app-updates ] && [ ! -L /opt/usr/cc2-rfid-app-updates ]; then
            (umask 077; mkdir /opt/usr/cc2-rfid-app-updates) || return 1
        fi
        "$BINDING_SOURCE/publisher" --publish "$BINDING_SOURCE/component" || return 1
    fi
    binding_verify_worker
}}
binding_prepare() {{
    binding_verify_worker && binding_run {destination}/worker --prepare "$BINDING_SOURCE/component/incoming" 3 || return 1
    BINDING_PREPARED=1
}}
binding_commit() {{
    binding_verify_worker && {destination}/worker --commit 3
}}
'''
    # Encoded payload remains outside the shell's in-memory function AST.
    tail = '\n' + BEGIN + '\n'
    for start in range(0, len(data), 4096):
        tail += "printf '" + ''.join('\\%03o' % b for b in data[start:start + 4096]) + "' || exit 1\n"
    tail += END + '\n'
    return code, tail, restore_functions
