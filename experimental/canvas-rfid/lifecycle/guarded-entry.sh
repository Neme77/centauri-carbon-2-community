#!/bin/sh
# Generated only from a qualified component profile. Constants below are
# deliberately not configured for installation in this source template.
set -eu
NATIVE=/opt/usr/cc2-reactor-runtime-v2/elegoo_printer
MANIFEST=/opt/usr/cc2-rfid/guarded.sha256
MANIFEST_SHA=UNCONFIGURED
CHECKER=/opt/usr/cc2-control/camera/cc2-camera-reap-check
CORE_PRELOADS=/opt/usr/cc2-reactor-runtime-v2/libcc2-reactor-v2.so:/opt/usr/cc2-rfid/libcc2-rfid.so
NETWORK_CONTROL=
NETWORK_LIBRARY=
LIBRARY_PATH=/opt/lib
stop() {
    printf '%s\n' 'STOP: mandatory Canvas RFID loader qualification failed; no native fallback' >&2
    exit 125
}
[ -z "${LD_PRELOAD:-}" ] || stop
[ -f "$MANIFEST" ] && [ ! -L "$MANIFEST" ] || stop
ACTUAL=$(sha256sum "$MANIFEST") || stop
ACTUAL=${ACTUAL%% *}
[ "$ACTUAL" = "$MANIFEST_SHA" ] || stop
sha256sum -c "$MANIFEST" || stop
network=0
if [ -n "$NETWORK_CONTROL" ]; then
    [ -n "$NETWORK_LIBRARY" ] || stop
    if "$NETWORK_CONTROL" --enabled; then
        # Preserve the qualified network startup behaviour: its watchdog owns
        # recovery and a failed ensure must not bypass the required RFID guard.
        "$NETWORK_CONTROL" --ensure || true
        network=1
    fi
else
    [ -z "$NETWORK_LIBRARY" ] || stop
fi
preloads=$CORE_PRELOADS
[ "$network" -eq 0 ] || preloads=$NETWORK_LIBRARY:$preloads
# Helpers remain inert. Check all shared-library dependencies before exporting
# the native reactor/network activation flags into the final exec only.
CC2_REACTOR_RUNTIME_V2=0 CC2_NETWORK_COMPAT_TRIAL=0 CC2_CAMERA_REAP_TRIAL=0 CC2_CAMERA_RECOVERY_TRIAL=0 LD_LIBRARY_PATH="$LIBRARY_PATH" LD_PRELOAD="$preloads" "$CHECKER" || stop
export CC2_REACTOR_RUNTIME_V2=1
export CC2_NETWORK_COMPAT_TRIAL="$network"
export LD_LIBRARY_PATH="$LIBRARY_PATH"
export LD_PRELOAD="$preloads"
exec "$NATIVE" "$@"
