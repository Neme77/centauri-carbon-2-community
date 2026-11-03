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
RUNNER="$HERE/pc-stage.sh"
[ -f "$RUNNER" ] || { echo 'Disk staging runner missing. Extract the complete package.';exit 1; }
if command -v sha256sum >/dev/null 2>&1;then
    runner_sha=$(sha256sum "$RUNNER" | awk '{print $1}')
elif command -v shasum >/dev/null 2>&1;then
    runner_sha=$(shasum -a 256 "$RUNNER" | awk '{print $1}')
else echo 'SHA-256 tool required';exit 1;fi
TOKEN=$(od -An -N16 -tx1 /dev/urandom | tr -d ' \n')
case "$TOKEN" in ''|*[!0-9a-f]*) exit 1;;esac
[ "${#TOKEN}" -eq 32 ] || exit 1
STAGE=/opt/usr/cc2-control-install-$TOKEN
ssh "$TARGET" "sh -s -- '$STAGE'" <<'CC2_PREPARE'
set -eu
umask 077
[ "$(id -u)" = 0 ]
[ -d /opt/usr ] && [ ! -L /opt/usr ]
awk '$2=="/opt/usr" && $3!="tmpfs" {ok=1} END {exit !ok}' /proc/mounts
free=$(df -k /opt/usr | awk 'END {print $4}')
case "$free" in ''|*[!0-9]*) exit 1;;esac
[ "$free" -ge 65536 ] || { echo 'STOP: less than 64 MiB free persistent storage';exit 1; }
available=$(awk '/^MemAvailable:/ {print $2}' /proc/meminfo)
case "$available" in ''|*[!0-9]*) exit 1;;esac
[ "$available" -ge 24576 ] || { echo 'STOP: less than 24 MiB available RAM';exit 1; }
mkdir "$1"
CC2_PREPARE
scp -O "$ARCHIVE" "$RUNNER" "$TARGET:$STAGE/"
ssh "$TARGET" "printf '%s\n' '$runner_sha  $STAGE/pc-stage.sh' | sha256sum -c - && sh '$STAGE/pc-stage.sh'"
echo "CC2 Control installed: http://$PRINTER_IP:8081"
