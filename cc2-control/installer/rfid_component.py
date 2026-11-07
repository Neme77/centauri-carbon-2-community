"""Private closed-profile RFID component, decoded as a bounded script stream.

The existing signed updater filename allowlist is unchanged. Encoded data sits
after the installer's final exit, outside any shell function's in-memory AST.
Only a freshly extracted, hash-verified publisher may publish the dormant tree.
"""
import hashlib
import io
import json
from pathlib import Path, PurePosixPath
import tarfile

BEGIN = '# CC2_RFID_DATA_BEGIN'
END = '# CC2_RFID_DATA_END'
ORDINARY_GUARD = '''if [ -e /opt/usr/cc2-rfid ] || [ -L /opt/usr/cc2-rfid ]; then
    echo 'STOP: managed RFID component present; use its qualified coordinated update or restore procedure before replacing the application' >&2
    exit 1
fi'''
RESTORE_GUARD = '''if [ -e /opt/usr/cc2-rfid/lifecycle ] || [ -L /opt/usr/cc2-rfid/lifecycle ]; then
    echo 'STOP: managed Canvas lifecycle present; retain the current app and finish verified stock restoration and host removal before application rollback' >&2
    exit 1
fi'''

def functions(runtime: Path, app_sha: str, defer_profile_check=False):
    data = runtime.read_bytes()
    if not data or len(data) > 8 * 1024 * 1024:
        raise ValueError('RFID runtime must be a bounded private component')
    with tarfile.open(fileobj=io.BytesIO(data), mode='r:gz') as archive:
        members = {}
        for entry in archive:
            path = PurePosixPath(entry.name)
            if (not entry.isfile() or path.is_absolute() or '..' in path.parts or
                    str(path) != entry.name or entry.name in members or entry.size > 4 * 1024 * 1024):
                raise ValueError('Unsafe RFID runtime member')
            if entry.name not in ('publisher', 'SHA256SUMS') and not entry.name.startswith('component/'):
                raise ValueError('Unexpected RFID runtime member')
            members[entry.name] = archive.extractfile(entry).read()
    if len(members) > 48 or sum(map(len, members.values())) > 8 * 1024 * 1024:
        raise ValueError('RFID runtime too large')
    required = {'SHA256SUMS', 'publisher', 'component/app-integration.json',
                'component/coordinator', 'component/capture', 'component/recovery-profile',
                'component/activate-on-ready'}
    if not required <= set(members):
        raise ValueError('Incomplete RFID runtime')
    publisher_bytes = members['publisher']
    if len(publisher_bytes) < 20 or publisher_bytes[:6] != b'\x7fELF\x01\x01' or publisher_bytes[18:20] != b'\x28\x00':
        raise ValueError('RFID publisher must be ARM32 little-endian ELF')
    expected = members.pop('SHA256SUMS').decode('ascii').splitlines()
    manifest = {}
    for line in expected:
        digest, name = line.split('  ', 1)
        if name in manifest or len(digest) != 64 or any(c not in '0123456789abcdef' for c in digest):
            raise ValueError('Invalid RFID runtime manifest')
        manifest[name] = digest
    if set(manifest) != set(members) or any(hashlib.sha256(v).hexdigest() != manifest[n] for n, v in members.items()):
        raise ValueError('RFID runtime checksum mismatch')
    metadata = json.loads(members['component/app-integration.json'])
    if metadata['protocol'] != 1 or metadata['app_sha256'] != app_sha:
        raise ValueError('RFID runtime belongs to another application')
    for key, name in [('coordinator_sha256', 'coordinator'), ('capture_sha256', 'capture'),
                      ('profile_sha256', 'recovery-profile')]:
        if metadata[key] != manifest['component/' + name]:
            raise ValueError('RFID component metadata does not bind its artifacts')
    activation = f'CC2-RFID-ACTIVATE-1\n{app_sha}\n{metadata["profile_sha256"]}\n'.encode()
    if members.get('component/activate-on-ready') != activation:
        raise ValueError('Missing application-bound post-install activation request')
    digest = hashlib.sha256(data).hexdigest()
    publisher = manifest['publisher']
    text = f'''rfid_unpack() {{
    RFID_SOURCE="$STAGE/rfid-component-$$"
    (umask 077; mkdir "$RFID_SOURCE") || return 1
    sed -n '/^{BEGIN}$/,/^{END}$/p' "$STAGE/install-on-printer.sh" | sed '/^#/d' | sh > "$RFID_SOURCE/runtime.tar.gz"
    [ "$(sha256sum "$RFID_SOURCE/runtime.tar.gz" | awk '{{print $1}}')" = {digest} ] || return 1
    tar -xzf "$RFID_SOURCE/runtime.tar.gz" -C "$RFID_SOURCE" || return 1
    (cd "$RFID_SOURCE" && sha256sum -c SHA256SUMS) || return 1
    [ "$(sha256sum "$RFID_SOURCE/publisher" | awk '{{print $1}}')" = {publisher} ] || return 1
    chmod 700 "$RFID_SOURCE/publisher" || return 1
    "$RFID_SOURCE/publisher" --check "$RFID_SOURCE/component"
}}
rfid_publish() {{
    "$RFID_SOURCE/publisher" --publish "$RFID_SOURCE/component"
}}
'''
    tail = '\n' + BEGIN + '\n'
    for start in range(0, len(data), 4096):
        chunk = ''.join('\\%03o' % byte for byte in data[start:start + 4096])
        tail += "printf '" + chunk + "' || exit 1\n"
    tail += END + '\n'
    if defer_profile_check:
        # The unified installer prepares the chamber baseline immediately before
        # publication. The publisher repeats its exact dependency check then.
        line='    "$RFID_SOURCE/publisher" --check "$RFID_SOURCE/component"\n'
        if text.count(line)!=1:raise ValueError('Unexpected RFID unpack template')
        text=text.replace(line,'    :\n',1)
    return text, tail
