#!/bin/sh
set -eu

BASE=/opt/inst/cc2-control
PERSIST=/opt/usr/cc2-control
MAX_WAIT=120
WAITED=0

logger -t cc2-control "Waiting for persistent storage /opt/usr"
while ! grep -q ' /opt/usr ' /proc/mounts; do
    sleep 2
    WAITED=$((WAITED + 2))
    if [ "$WAITED" -ge "$MAX_WAIT" ]; then
        logger -t cc2-control "Persistent storage /opt/usr unavailable after ${MAX_WAIT}s"
        exit 1
    fi
done

mkdir -p "$PERSIST"
chmod 755 "$PERSIST"
if [ ! -f "$PERSIST/material-presets.json" ] && [ -f "$BASE/defaults/material-presets.json" ]; then
    cp "$BASE/defaults/material-presets.json" "$PERSIST/material-presets.json"
    chmod 644 "$PERSIST/material-presets.json"
fi

WAITED=0
logger -t cc2-control "Waiting for elegoo_printer"
while ! pidof elegoo_printer >/dev/null 2>&1; do
    sleep 2
    WAITED=$((WAITED + 2))
    if [ "$WAITED" -ge "$MAX_WAIT" ]; then
        logger -t cc2-control "elegoo_printer unavailable after ${MAX_WAIT}s"
        exit 1
    fi
done

logger -t cc2-control "elegoo_printer detected; waiting for hardware initialization"

# Allow the vendor hardware initialization to settle before connecting.
sleep 30
logger -t cc2-control "Starting CC2 Control for the community firmware"

exec "$BASE/cc2-control" \
    --port 8081 \
    --panda-port 7125 \
    --web-root "$BASE/web" \
    --config "$PERSIST/cc2-control.conf" \
    --presets "$PERSIST/material-presets.json" \
    --preferences "$PERSIST/ui-preferences.json"
