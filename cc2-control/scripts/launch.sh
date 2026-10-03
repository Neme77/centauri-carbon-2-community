#!/bin/sh

MAX_WAIT=120
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
sleep 60
logger -t cc2-control "Starting CC2 Control"
exec /opt/usr/cc2-control/start.sh
