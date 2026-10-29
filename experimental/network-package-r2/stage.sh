#!/bin/sh
set -eu
umask 077
BASE=/tmp/cc2-network-compat-r1
ARCHIVE=/tmp/cc2-network-compat-r1.tar.gz
fail(){ echo "STOP: $*" >&2; exit 1; }
[ "$(id -u)" = 0 ] || fail 'Root required'
[ -f "$ARCHIVE" ] && [ ! -L "$ARCHIVE" ] || fail 'Archive is not a regular file'
[ "$(sha256sum "$ARCHIVE" | awk '{print $1}')" = '3c3a202c422eb380570f8395142c4785c4bdfd71a8e2c432796c497eed1c92a7' ] || fail 'Archive checksum differs'
# Never overwrite a shared object already mapped by a running process.
for pid in $(pidof ai_camera elegoo_printer 2>/dev/null || true);do
    if grep -F 'libcc2-network-compat-r1.so' "/proc/$pid/maps" >/dev/null;then
        fail 'Network trial library already mapped; use its status/restore commands. Do not overwrite it.'
    fi
done
[ ! -L "$BASE" ] || fail 'Unexpected trial-directory symlink'
if [ -f "$BASE/active" ];then
    [ ! -L "$BASE/active" ] || fail 'Unexpected active-state symlink'
    previous=$(cat "$BASE/active")
    case "$previous" in "$BASE"/state.*) ;; *) fail 'Unexpected previous state';; esac
    suffix=${previous#"$BASE"/state.}
    case "$suffix" in ''|*[!0-9.]*) fail 'Invalid previous state name';; esac
    if [ -f "$previous/watch.pid" ];then
        watch_pid=$(cat "$previous/watch.pid")
        case "$watch_pid" in ''|*[!0-9]*) fail 'Invalid previous watchdog PID';; esac
        if kill -0 "$watch_pid" 2>/dev/null;then fail 'Previous trial watchdog still alive; do not overwrite payload';fi
    fi
fi
mkdir -p "$BASE"
chmod 700 "$BASE"
tar -xzf "$ARCHIVE" -C "$BASE"
sh -n "$BASE/manage.sh"
sh -n "$BASE/preflight.sh"
sh -n "$BASE/thermal_selftest.sh"
sh "$BASE/manage.sh" check
