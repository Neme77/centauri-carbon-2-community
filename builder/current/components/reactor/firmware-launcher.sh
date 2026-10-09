#!/bin/sh
# Firmware entry: keep the qualified runtime ABI and executable path unchanged.
set -eu
SOURCE=/opt/inst/cc2-reactor
BASE=/opt/usr/cc2-reactor-runtime-v2
PREPARE_ONLY=0
if [ "${1:-}" = --prepare-only ]; then
    PREPARE_ONLY=1
    shift
fi
EXPECTED=c21126e78a63ac9140e7347c56f363d5e513495c58c06fc17e8ef97846f3c0a3
fallback() {
    logger -t cc2-reactor 'Experimental callback disabled or unavailable; starting vendor binary'
    [ "$PREPARE_ONLY" -eq 0 ] || exit 0
    exec /opt/bin/elegoo_printer "$@"
}
WAITED=0
while ! grep -q ' /opt/usr ' /proc/mounts; do
    sleep 2
    WAITED=$((WAITED+2))
    [ "$WAITED" -lt 120 ] || fallback "$@"
done
[ ! -e /opt/usr/cc2-reactor-disabled ] || fallback "$@"
[ -z "${LD_PRELOAD:-}" ] || fallback "$@"
[ "$(sha256sum /opt/bin/elegoo_printer | awk '{print $1}')" = "$EXPECTED" ] || fallback "$@"
[ "$(sha256sum /opt/lib/libcolib.so | awk '{print $1}')" = 14288a4a73c339b039dd89597618ebb05702f90d668b76f74019d5d15ce67403 ] || fallback "$@"
(cd "$SOURCE" && sha256sum -c SHA256SUMS >/dev/null 2>&1) || fallback "$@"
PAYLOAD=$(sha256sum "$SOURCE/SHA256SUMS" | awk '{print $1}')
CURRENT=$(cat "$BASE/firmware-payload.sha256" 2>/dev/null || true)
if [ "$CURRENT" != "$PAYLOAD" ]; then
    STAGE=/opt/usr/.cc2-reactor-firmware-$$
    mkdir "$STAGE" || fallback "$@"
    chmod 700 "$STAGE"
    if ! cp "$SOURCE/"* "$STAGE/" || ! cp /opt/bin/elegoo_printer "$STAGE/elegoo_printer"; then
        rm -rf "$STAGE"
        fallback "$@"
    fi
    chmod 700 "$STAGE/launcher.sh" "$STAGE/cc2-reactor-bridge-test" "$STAGE/elegoo_printer"
    if ! sh "$STAGE/preflight.sh" > "$STAGE/preflight.log" 2>&1; then
        logger -t cc2-reactor 'Native bridge preflight failed; callback not activated'
        rm -rf "$STAGE"
        fallback "$@"
    fi
    printf '%s\n' "$PAYLOAD" > "$STAGE/firmware-payload.sha256"
    if [ -e "$BASE" ]; then
        mv "$BASE" "/opt/usr/cc2-reactor-before-firmware-$$" || fallback "$@"
    fi
    mv "$STAGE" "$BASE" || fallback "$@"
fi
(cd "$BASE" && sha256sum -c SHA256SUMS >/dev/null 2>&1) || fallback "$@"
[ "$(sha256sum "$BASE/elegoo_printer" | awk '{print $1}')" = "$EXPECTED" ] || fallback "$@"
[ "$PREPARE_ONLY" -eq 0 ] || exit 0
rm -f "$BASE/activation.marker"
exec "$BASE/launcher.sh" "$@"
