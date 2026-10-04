#!/bin/sh
set -eu
RESET_HOST_KEY=0
if [ "${1:-}" = "--reset-host-key" ]; then
    RESET_HOST_KEY=1
    shift
fi
[ "$#" -le 1 ] || {
    echo "Usage: $0 [--reset-host-key] [printer-address]" >&2
    exit 2
}
PRINTER_IP="${1:-}"
if [ -z "$PRINTER_IP" ]; then printf 'Printer IP address: '; read -r PRINTER_IP; fi
case "$PRINTER_IP" in *[!A-Za-z0-9.-]*|'') echo 'Invalid printer address.' >&2; exit 2;; esac
command -v ssh >/dev/null || { echo 'ssh is required.' >&2; exit 2; }
command -v scp >/dev/null || { echo 'scp is required.' >&2; exit 2; }
HERE="$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)"
ARCHIVE="$HERE/cc2-control-payload.tar.gz"
test -f "$ARCHIVE" || { echo 'Payload archive is missing.' >&2; exit 2; }
TARGET="root@$PRINTER_IP"
if [ "$RESET_HOST_KEY" -eq 1 ]; then
    command -v ssh-keygen >/dev/null || {
        echo 'ssh-keygen is required to reset the saved host key.' >&2
        exit 2
    }
    printf 'The saved SSH host key for %s will be removed.\n' "$PRINTER_IP"
    echo 'Continue only if you expected the printer host key to change.'
    echo 'OpenSSH will ask you to verify and accept the new fingerprint.'
    printf 'Type the printer address (%s) to confirm: ' "$PRINTER_IP"
    CONFIRMATION=''
    if ! read -r CONFIRMATION || [ "$CONFIRMATION" != "$PRINTER_IP" ]; then
        echo 'Host-key reset cancelled. No installation was performed.' >&2
        exit 1
    fi
    ssh-keygen -R "$PRINTER_IP" || {
        echo 'Host-key reset failed. No installation was performed.' >&2
        exit 1
    }
fi
scp "$ARCHIVE" "$TARGET:/tmp/cc2-control-payload.tar.gz"
ssh "$TARGET" 'mkdir -p /tmp/cc2-control-update && tar -xzf /tmp/cc2-control-payload.tar.gz -C /tmp/cc2-control-update && chmod 755 /tmp/cc2-control-update/install-on-printer.sh /tmp/cc2-control-update/cc2-control && sh /tmp/cc2-control-update/install-on-printer.sh'
echo "CC2 Control installed: http://$PRINTER_IP:8081"
