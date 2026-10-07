#!/bin/sh
# This entry is on the same filesystem as the vendor init, so it can wait for
# persistent storage before selecting the experimental module. No respawn loop.
set -eu
BASE=/opt/usr/cc2-reactor-runtime-v2
EXPECTED=c21126e78a63ac9140e7347c56f363d5e513495c58c06fc17e8ef97846f3c0a3
WAITED=0
while ! grep -q ' /opt/usr ' /proc/mounts; do
    sleep 2
    WAITED=$((WAITED+2))
    if [ "$WAITED" -ge 120 ]; then
        logger -t cc2-reactor 'Storage unavailable; starting unmodified vendor binary'
        exec /opt/bin/elegoo_printer "$@"
    fi
done
if [ -x "$BASE/launcher.sh" ] &&
    [ "$(sha256sum /opt/bin/elegoo_printer | awk '{print $1}')" = "$EXPECTED" ] &&
    [ "$(sha256sum "$BASE/elegoo_printer" | awk '{print $1}')" = "$EXPECTED" ] &&
    (cd "$BASE" && sha256sum -c SHA256SUMS >/dev/null 2>&1); then
    exec "$BASE/launcher.sh" "$@"
fi
logger -t cc2-reactor 'Test disabled: files missing or incompatible; starting unmodified vendor binary'
exec /opt/bin/elegoo_printer "$@"
