#!/bin/sh
set -eu
BASE=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
[ "$(sha256sum /opt/lib/libcolib.so | awk '{print $1}')" = 14288a4a73c339b039dd89597618ebb05702f90d668b76f74019d5d15ce67403 ] || { echo 'STOP: libco diversa'; exit 1; }
cd "$BASE"
sha256sum -c SHA256SUMS
CC2_REACTOR_RUNTIME_V2=0 LD_PRELOAD="$BASE/libcc2-reactor-v2.so" LD_LIBRARY_PATH=/opt/lib:/lib:/usr/lib /lib/ld-linux-armhf.so.3 --list "$BASE/cc2-reactor-bridge-test"
CC2_REACTOR_RUNTIME_V2=0 LD_PRELOAD="$BASE/libcc2-reactor-v2.so" LD_LIBRARY_PATH=/opt/lib:/lib:/usr/lib "$BASE/cc2-reactor-bridge-test"
echo 'PREFLIGHT ARM PASS: nessun servizio modificato.'
