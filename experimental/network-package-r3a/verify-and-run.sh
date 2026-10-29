#!/bin/sh
set -eu
umask 077
BASE=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
[ "$BASE" = /opt/usr/cc2-control-network-trial-r3 ] && [ ! -L "$BASE" ] || { echo 'STOP: unexpected stage'; exit 1; }
[ "$(sha256sum "$BASE/run-install.sh" | awk '{print $1}')" = cec46f3256189f80693b667407eab505ea25b9983d2e2829e2fa519715856108 ] || { echo 'STOP: runner mismatch'; exit 1; }
exec sh "$BASE/run-install.sh"
