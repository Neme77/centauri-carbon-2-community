#!/bin/sh
set -eu
BASE=/opt/usr/cc2-reactor-runtime-v2
EXPECTED=c21126e78a63ac9140e7347c56f363d5e513495c58c06fc17e8ef97846f3c0a3
[ "$(sha256sum "$BASE/elegoo_printer" | awk '{print $1}')" = "$EXPECTED" ] || exit 125
(cd "$BASE" && sha256sum -c SHA256SUMS >/dev/null) || exit 125
[ -z "${LD_PRELOAD:-}" ] || exit 125
export CC2_REACTOR_RUNTIME_V2=1
export LD_PRELOAD="$BASE/libcc2-reactor-v2.so"
# Preserve the working directory selected by the vendor init.
exec "$BASE/elegoo_printer" "$@"
