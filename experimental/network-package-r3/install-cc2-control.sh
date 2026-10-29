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
ssh "$TARGET" 'set -eu; umask 077; stage=/opt/usr/cc2-control-network-trial-r3; [ ! -e "$stage" ] && [ ! -L "$stage" ] || { echo "STOP: previous R3 stage exists; preserve its log"; exit 1; }; free=$(df -k /opt/usr | awk '\''NR==2 {print $4}'\''); case "$free" in ""|*[!0-9]*) echo "STOP: free disk space unavailable"; exit 1;; esac; [ "$free" -ge 65536 ] || { echo "STOP: less than 64 MiB free on /opt/usr"; exit 1; }; mkdir "$stage"; echo "Disk staging prepared"'
scp -O "$ARCHIVE" "$HERE/run-install.sh" "$TARGET:/opt/usr/cc2-control-network-trial-r3/"
ssh "$TARGET" 'set -eu; [ "$(sha256sum /opt/usr/cc2-control-network-trial-r3/run-install.sh | awk '\''{print $1}'\'')" = cec46f3256189f80693b667407eab505ea25b9983d2e2829e2fa519715856108 ] || { echo "STOP: runner mismatch"; exit 1; }; sh /opt/usr/cc2-control-network-trial-r3/run-install.sh'
echo "CC2 Control installed: http://$PRINTER_IP:8081"
