#!/bin/sh
# Reuse the exact qualified loopback readiness reader already present in this
# closed network profile. It performs no network selection or native restart.
# Freshness <=5 s, idle/standby-or-complete, both targets zero and <=50 C.
# This is one observation, NOT an update lock or installation authorization.
set -eu
CONTROL=/opt/usr/cc2-network-manager/control
CONTROL_SHA=2a4dd2dd8e23aa3446348973ba6b8fa90a1fda16fcd96626015f8f0b816e9d62
stop(){ printf '%s\n' 'STOP: qualified_live_idle_cold_guard_failed' >&2;exit 1; }
[ "$#" -eq 1 ] && [ "$1" = --check ] || stop
[ -z "${LD_PRELOAD:-}" ] || stop
[ -f "$CONTROL" ] && [ ! -L "$CONTROL" ] || stop
actual=$(sha256sum "$CONTROL") || stop
[ "${actual%% *}" = "$CONTROL_SHA" ] || stop
exec "$CONTROL" --guard 8081
