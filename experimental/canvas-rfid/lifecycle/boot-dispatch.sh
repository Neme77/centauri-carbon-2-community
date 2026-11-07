#!/bin/sh
# Not installable until the profile builder replaces these exact constants.
# Used by BOTH native entry routes; neither route may retain a vendor fallback
# around this dispatcher while a modified Canvas is possible.
set -eu
WORKER=/opt/usr/cc2-rfid/recover-worker
WORKER_SHA=UNCONFIGURED
STOCK_ENTRY=/opt/usr/cc2-rfid/stock-entry.sh
GUARDED_ENTRY=/opt/usr/cc2-rfid/guarded-entry.sh
stop() {
    printf '%s\n' 'STOP: Canvas boot transaction unresolved; native startup blocked' >&2
    exit 125
}
[ -z "${LD_PRELOAD:-}" ] || stop
[ -f "$WORKER" ] && [ ! -L "$WORKER" ] || stop
ACTUAL=$(sha256sum "$WORKER") || stop
ACTUAL=${ACTUAL%% *}
[ "$ACTUAL" = "$WORKER_SHA" ] || stop
status=0
"$WORKER" --boot-policy || status=$?
if [ "$status" -eq 20 ]; then
    # One bounded stock recovery attempt. A failure never falls through to a
    # normal native start and is never retried indefinitely during this boot.
    "$WORKER" --recover-original || stop
    status=0
    "$WORKER" --boot-policy || status=$?
    [ "$status" -eq 10 ] || stop
fi
case "$status" in
    0) exec "$STOCK_ENTRY" "$@" ;;
    10) exec "$GUARDED_ENTRY" "$@" ;;
    *) stop ;;
esac
