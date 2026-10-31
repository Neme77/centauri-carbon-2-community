"""Embed the unchanged qualified network runtime in the signed installer.

Keep the existing verifier's filename allowlist. Unsupported native profiles
retain the normal application update without changing their network startup.
"""
import hashlib
import io
from pathlib import Path
import tarfile

RUNTIME_SHA = '95db87b35dcf6799d5d57ec8ad046455c97e5cb297cc07ea4a2eeda90fb367de'
HELPER_SHA = '192ee3df47f6543e59e036a11bd242a0d0e070e7e0dc67b1d5609116903cf23d'

def functions(runtime: Path):
    data = runtime.read_bytes()
    if hashlib.sha256(data).hexdigest() != RUNTIME_SHA:
        raise ValueError('Unqualified Connections runtime')
    with tarfile.open(fileobj=io.BytesIO(data), mode='r:gz') as archive:
        for entry in archive:
            if not (entry.isfile() or entry.isdir()) or entry.name.startswith('/') or '..' in Path(entry.name).parts:
                raise ValueError('Unsafe Connections runtime member')
        helper = archive.extractfile('control').read()
        if hashlib.sha256(helper).hexdigest() != HELPER_SHA:
            raise ValueError('Unqualified Connections helper')
    lines = ['''network_profile_supported() {
    [ "$(uname -r)" = 5.4.61 ] && [ "$(uname -m)" = armv7l ] || return 1
    for item in '/etc/init.d/printer e7537298955e23b7a03cab4c8d7bd241c1dfcc070883cdf51c4889cd255d1bc4' '/bin/wifi_daemon 8e19a620921c6f4f21bde910cc3d960af5400d223493041eb96f1e921dcbe3c1' '/opt/usr/cc2-reactor-runtime-v2/launcher.sh a260865964759898cf47ed8e30e20eb8981506c1a22ada32611b9911b36b47fc'; do
        file=${item%% *}; expected=${item#* }
        [ -f "$file" ] && [ ! -L "$file" ] || return 1
        actual=$(sha256sum "$file" | awk '{print $1}')
        [ "$actual" = "$expected" ] || return 1
    done
    config=$(zcat /proc/config.gz | sha256sum) || return 1
    [ "${config%% *}" = 8fa0a87f203d69cb9c4e027e8db11a9a0d92c5d2ea8ae83f87b34714aae7ec4a ]
}
network_install_component() {
    if ! network_profile_supported; then
        if [ -e /opt/usr/cc2-network-manager ] || [ -L /opt/usr/cc2-network-manager ]; then
            echo 'STOP: installed Connections component has an unsupported native profile' >&2
            return 1
        fi
        echo 'CONNECTIONS_COMPONENT_SKIPPED: native profile not qualified; application updated, native Wi-Fi unchanged.'
        return 0
    fi
    NETWORK_SOURCE="$STAGE/connections-component-$$"
    (umask 077; mkdir "$NETWORK_SOURCE") || return 1''']
    for start in range(0, len(data), 4096):
        chunk = ''.join('\\%03o' % byte for byte in data[start:start + 4096])
        lines.append("    printf '" + chunk + "' " + ('>' if start == 0 else '>>') + ' "$NETWORK_SOURCE/payload.tar.gz" || return 1')
    lines += [
        f'    [ "$(sha256sum "$NETWORK_SOURCE/payload.tar.gz" | awk \'{{print $1}}\')" = {RUNTIME_SHA} ] || return 1',
        '    tar -xzf "$NETWORK_SOURCE/payload.tar.gz" -C "$NETWORK_SOURCE" || return 1',
        '    (cd "$NETWORK_SOURCE" && sha256sum -c SHA256SUMS) || return 1',
        '    sh "$NETWORK_SOURCE/install.sh" || return 1',
        "    echo 'CONNECTIONS_COMPONENT_INSTALLED=1'",
        "    echo 'On first USB Ethernet activation, Canvas may flash red while services realign, then returns to operation.'",
        '    case "$NETWORK_SOURCE" in "$STAGE"/connections-component-[0-9]*)',
        '        if [ -d "$NETWORK_SOURCE" ] && [ ! -L "$NETWORK_SOURCE" ]; then',
        '            rm -rf "$NETWORK_SOURCE" || echo "Installed; temporary component retained: $NETWORK_SOURCE"',
        '        else echo "Installed; unexpected temporary component retained"; fi;;',
        '      *) echo "Installed; unexpected temporary component retained";; esac',
        '    return 0',
        '}', '']
    return '\n'.join(lines)
