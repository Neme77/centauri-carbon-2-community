#!/bin/sh
# Immutable delegate generated with the qualified startup worker fingerprint.
# Both native launch routes pass here; there is no vendor fallback.
set -eu
WORKER=/opt/usr/cc2-rfid/startup-worker
WORKER_SHA=UNCONFIGURED
stop(){ printf '%s\n' 'STOP: qualified Canvas startup resumer unavailable; native startup blocked' >&2;exit 125; }
[ -z "${LD_PRELOAD:-}" ] || stop
[ -f "$WORKER" ] && [ ! -L "$WORKER" ] || stop
actual=$(sha256sum "$WORKER") || stop
[ "${actual%% *}" = "$WORKER_SHA" ] || stop
if [ "${1:-}" = --prepare-only ];then
    [ "$#" -eq 1 ] || stop
    exec "$WORKER" --prepare-only
fi
exec "$WORKER" --boot "$@"
