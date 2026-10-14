#!/bin/sh
set -eu
BASE=/opt/usr/cc2-reactor-runtime-v2
EXPECTED=19712b5943b10bb8cb6aab9a4ed498ee962b28538b23c91b7ed003eb9e590745
[ "$(sha256sum "$BASE/elegoo_printer" | awk '{print $1}')" = "$EXPECTED" ] || exit 125
(cd "$BASE" && sha256sum -c SHA256SUMS >/dev/null) || exit 125
[ -z "${LD_PRELOAD:-}" ] || exit 125
export CC2_REACTOR_RUNTIME_V2=1
export LD_PRELOAD="$BASE/libcc2-reactor-v2.so"
# Preserve the working directory selected by the vendor init.
exec "$BASE/elegoo_printer" "$@"
