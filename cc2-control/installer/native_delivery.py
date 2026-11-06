"""One installer delivery: select fresh, active-upgrade or verified current state.

Selection only chooses which already guarded operation to run. File existence
never authorizes replacement: each route must pass its pinned native preflight.
"""
import hashlib
import io
import json
import tarfile
from app_binding_bundle import unique_pairs
from app_binding_component import functions as binding_functions
from cavity_component import functions as cavity_functions
from rfid_component import functions as rfid_functions


class Blob:
    def __init__(self,data):self.data=data
    def read_bytes(self):return self.data


def functions(runtime,app_sha):
    data=runtime.read_bytes()
    if not data or len(data)>16*1024*1024:raise ValueError('Unbounded unified native runtime')
    files={};total=0
    with tarfile.open(fileobj=io.BytesIO(data),mode='r:gz') as archive:
        for item in archive:
            total+=item.size
            if (not item.isfile() or item.name not in ['delivery.json','rfid.tar.gz','cavity.tar.gz','binding.tar.gz'] or
                item.name in files or item.size<1 or item.size>8*1024*1024 or total>16*1024*1024 or
                item.uid or item.gid or item.mode!=0o600):raise ValueError('Unsafe unified native member')
            files[item.name]=archive.extractfile(item).read()
    if set(files)!={'delivery.json','rfid.tar.gz','cavity.tar.gz','binding.tar.gz'}:raise ValueError('Incomplete unified delivery')
    metadata=json.loads(files['delivery.json'],object_pairs_hook=unique_pairs)
    if (set(metadata)!={'protocol','app_sha256','runtime_sha256'} or type(metadata['protocol']) is not int or
        metadata['protocol']!=1 or metadata['app_sha256']!=app_sha or
        metadata['runtime_sha256']!={n:hashlib.sha256(b).hexdigest() for n,b in files.items() if n!='delivery.json'}):
        raise ValueError('Unified native binding mismatch')
    rfid,rtail=rfid_functions(Blob(files['rfid.tar.gz']),app_sha,defer_profile_check=True)
    cavity,ctail=cavity_functions(Blob(files['cavity.tar.gz']))
    binding,btail,restore=binding_functions(Blob(files['binding.tar.gz']),app_sha,defer_publication=True)
    with tarfile.open(fileobj=io.BytesIO(files['binding.tar.gz']),mode='r:gz') as archive:
        worker_sha=hashlib.sha256(archive.extractfile('component/worker').read()).hexdigest()
        incoming={n:archive.extractfile('component/incoming/'+n).read() for n in
            ['coordinator','capture','app-integration.json','activate-on-ready']}
    with tarfile.open(fileobj=io.BytesIO(files['rfid.tar.gz']),mode='r:gz') as archive:
        if any(archive.extractfile('component/'+n).read()!=b for n,b in incoming.items()):
            raise ValueError('First-install and upgrade must deliver identical application-bound RFID files')
    code=rfid+cavity+binding+f'''NATIVE_ROUTE=''
NATIVE_PREPARED=0
native_current() {{
    binding_run "$BINDING_SOURCE/component/worker" --check-current 3
}}
native_unpack() {{
    exec 3< "$LOCK"
    if [ -e /opt/usr/cc2-rfid ] || [ -L /opt/usr/cc2-rfid ]; then
        binding_unpack || return 1
        if [ "$(sha256sum "$TARGET/cc2-control" | awk '{{print $1}}')" = {app_sha} ]; then
            native_current || return 1
            NATIVE_ROUTE=current
        else
            binding_publish || return 1
            NATIVE_ROUTE=upgrade
        fi
    else
        cavity_unpack && rfid_unpack || return 1
        NATIVE_ROUTE=fresh
    fi
}}
native_prepare() {{
    (umask 077; printf '%s\\n' "$NATIVE_ROUTE" > "$BACKUP/native-route") || return 1
    if [ "$NATIVE_ROUTE" != fresh ]; then
        cp "$BINDING_SOURCE/component/worker" "$BACKUP/native-check-worker" &&
        chmod 700 "$BACKUP/native-check-worker" || return 1
    fi
    NATIVE_PREPARED=1
    case "$NATIVE_ROUTE" in
        fresh) cavity_prepare && rfid_publish;;
        upgrade) binding_prepare;;
        current) native_current;;
        *) return 1;;
    esac
}}
native_commit() {{
    case "$NATIVE_ROUTE" in fresh) :;; upgrade) binding_commit;; current) native_current;; *) return 1;;esac
}}
native_rollback() {{
    [ "$NATIVE_PREPARED" = 1 ] || return 0
    case "$NATIVE_ROUTE" in
        fresh)
            if [ -e /opt/usr/cc2-rfid ] || [ -L /opt/usr/cc2-rfid ]; then
                "$RFID_SOURCE/publisher" --retire-dormant "$RFID_SOURCE/component" || return 1
            fi;;
        upgrade) [ "$BINDING_PREPARED" = 0 ] || binding_rollback;;
        current) :;;
        *) return 1;;
    esac
}}
'''
    restore+=f'''native_restore_guard() {{
    exec 3< "$LOCK"
    case "$(cat "$BACKUP/native-route")" in
        upgrade) binding_verify_worker;;
        current)
            [ ! -L "$BACKUP/native-check-worker" ] &&
            [ "$(sha256sum "$BACKUP/native-check-worker" | awk '{{print $1}}')" = {worker_sha} ] &&
            "$BACKUP/native-check-worker" --check-current 3;;
        fresh)
            [ ! -e /opt/usr/cc2-rfid ] && [ ! -L /opt/usr/cc2-rfid ] || {{
                echo 'STOP: finish managed Canvas stock restoration and host removal before application rollback' >&2
                return 1
            }};;
        *) return 1;;
    esac
}}
native_restore_binding() {{
    case "$(cat "$BACKUP/native-route")" in upgrade) binding_rollback;; current|fresh) :;; *) return 1;;esac
}}
'''
    return {'functions':code,'tail':rtail+ctail+btail,'restore_functions':restore,
        'preflight':'native_unpack || exit 1','prepare':'native_prepare || exit 1',
        'commit':'native_commit || exit 1','restore_guard':'native_restore_guard || exit 1',
        'restore_rollback':'native_restore_binding || exit 1',
        'rollback':'''if ! native_rollback; then
                echo 'Native rollback incomplete; retain current app, barrier and backups.' >&2
                exit 1
            fi''',
        'unchanged_rollback':'''if [ "$PASSED" -eq 0 ] && [ "$STOPPED" -eq 0 ]; then
        native_rollback || { echo 'Native preparation recovery incomplete; retain barrier and backups.' >&2; exit 1; }
    fi'''}
