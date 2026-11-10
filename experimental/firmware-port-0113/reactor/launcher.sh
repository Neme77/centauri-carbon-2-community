#!/bin/sh
set -eu
BASE=/opt/usr/cc2-reactor-runtime-v2
EXPECTED=688b7bf8a27762d56b35d9738ea5b0c3d45580b402e122b0f77ee49dab1a8cfd
[ "$(sha256sum "$BASE/elegoo_printer" | awk '{print $1}')" = "$EXPECTED" ] || exit 125
(cd "$BASE" && sha256sum -c SHA256SUMS >/dev/null) || exit 125
[ -z "${LD_PRELOAD:-}" ] || exit 125
export CC2_REACTOR_RUNTIME_V2=1
export LD_PRELOAD="$BASE/libcc2-reactor-v2.so"
# Preserve the working directory selected by the vendor init.
exec "$BASE/elegoo_printer" "$@"
