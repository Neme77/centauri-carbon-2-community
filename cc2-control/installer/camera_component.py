"""Embed the pinned camera component inside the authenticated installer script.

The existing signed-update format only permits known application filenames.
Keeping the component inside that signed script supports already installed
verifiers without extending their extraction allowlist. No separate network
download or printer-side base64/Python/OpenSSL dependency is introduced.
"""
import gzip
import hashlib
import io
from pathlib import Path
import tarfile

PINS = {
    'libcc2-camera-reap-r2.so': '8ef3a5c215462e3c9f32c3d4b85ea23e27c41e330d53b22d7cdeca9d100d016a',
    'libcc2-camera-recovery-r2.so': '08f2455a99fa5294153ae64ce34117c02f4f8d2dff8527dfd8386aae361a289a',
    'cc2-camera-reap-check': 'dce1180e3c04512fea1c4f9868b131f3fbe9bda187ebc9342f9db633f0a57ea0',
}

RESTORE = '''camera_restore_startup() {
    camera_backup=$1
    camera_init=/etc/init.d/printer
    [ -f "$camera_backup/printer-camera.before" ] && [ ! -L "$camera_backup/printer-camera.before" ] || return 1
    [ -f "$camera_init" ] && [ ! -L "$camera_init" ] || return 1
    camera_before=$(sha256sum "$camera_backup/printer-camera.before" | awk '{print $1}')
    camera_current=$(sha256sum "$camera_init" | awk '{print $1}')
    [ "$camera_current" != "$camera_before" ] || return 0
    case "$camera_before" in
      bcabb1fa405b066b06c229c6d8cc6e474a0ccc57e31a1b5b387d3b55945be563|01924b3dc427972838e995ac6763b566b85754a162d60fd6728553a405cd00cc) ;;
      *) return 1;;
    esac
    [ -f "$TARGET/camera/patched.sha256" ] && [ ! -L "$TARGET/camera/patched.sha256" ] || return 1
    [ "$camera_current" = "$(cat "$TARGET/camera/patched.sha256")" ] || return 1
    camera_restore_dir=/etc/init.d/.cc2-camera-restore-$$
    mkdir "$camera_restore_dir" || return 1
    if ! cp -p "$camera_backup/printer-camera.before" "$camera_restore_dir/printer"; then
        rmdir "$camera_restore_dir"; return 1
    fi
    if [ "$(sha256sum "$camera_init" | awk '{print $1}')" != "$camera_current" ] || ! sh -n "$camera_restore_dir/printer"; then
        rm -f "$camera_restore_dir/printer"; rmdir "$camera_restore_dir"; return 1
    fi
    if ! mv "$camera_restore_dir/printer" "$camera_init"; then
        rm -f "$camera_restore_dir/printer"; rmdir "$camera_restore_dir"; return 1
    fi
    rmdir "$camera_restore_dir"
    sync
    [ "$(sha256sum "$camera_init" | awk '{print $1}')" = "$camera_before" ]
}
'''


def component_archive(source: Path, runtime: Path):
    files = {name: (source / name).read_bytes() for name in ('common.sh', 'manage.sh', 'boot-camera.sh')}
    for name, expected in PINS.items():
        data = (runtime / name).read_bytes()
        if hashlib.sha256(data).hexdigest() != expected:
            raise ValueError('Unqualified camera runtime: ' + name)
        files[name] = data
    files['SHA256SUMS'] = ''.join(hashlib.sha256(data).hexdigest() + '  ' + name + '\n'
                                for name, data in sorted(files.items())).encode()
    buf = io.BytesIO()
    with tarfile.open(fileobj=buf, mode='w', format=tarfile.USTAR_FORMAT) as archive:
        for name, data in sorted(files.items()):
            entry = tarfile.TarInfo(name)
            entry.size, entry.mtime, entry.uid, entry.gid = len(data), 0, 0, 0
            entry.mode = 0o700 if name.endswith('.sh') or name == 'cc2-camera-reap-check' else 0o600
            archive.addfile(entry, io.BytesIO(data))
    return gzip.compress(buf.getvalue(), mtime=0)


def functions(source: Path, runtime: Path):
    data = component_archive(source, runtime)
    lines = [RESTORE, 'camera_unpack() {', '    CAMERA_SOURCE="$STAGE/camera-component-$$"',
             '    (umask 077; mkdir "$CAMERA_SOURCE") || return 1']
    for start in range(0, len(data), 4096):
        chunk = ''.join('\\%03o' % byte for byte in data[start:start + 4096])
        lines.append("    printf '" + chunk + "' " + ('>' if start == 0 else '>>') + ' "$CAMERA_SOURCE/payload.tar.gz" || return 1')
    lines += [f'    [ "$(sha256sum "$CAMERA_SOURCE/payload.tar.gz" | awk \'{{print $1}}\')" = "{hashlib.sha256(data).hexdigest()}" ] || return 1',
              '    tar -xzf "$CAMERA_SOURCE/payload.tar.gz" -C "$CAMERA_SOURCE" || return 1',
              '    rm -f "$CAMERA_SOURCE/payload.tar.gz"',
              '    (cd "$CAMERA_SOURCE" && sha256sum -c SHA256SUMS) || return 1', '}', '']
    return '\n'.join(lines)
