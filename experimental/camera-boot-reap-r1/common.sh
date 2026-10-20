#!/bin/sh
# Fixed targets: no environment override for compatibility or filesystem paths.
BASE=/opt/usr/cc2-camera-reap-boot-r1
INIT=/etc/init.d/printer
CAMERA=/opt/bin/ai_camera
LOADER=/lib/ld-linux-armhf.so.3
INIT_SHA=bcabb1fa405b066b06c229c6d8cc6e474a0ccc57e31a1b5b387d3b55945be563
CAMERA_SHA=ec5633b17de329d5b18c3f8d8de6ebf389391a04193536d976ba167022d07b99
LIB_SHA=8ef3a5c215462e3c9f32c3d4b85ea23e27c41e330d53b22d7cdeca9d100d016a
HELPER_SHA=dce1180e3c04512fea1c4f9868b131f3fbe9bda187ebc9342f9db633f0a57ea0
MARKER=/tmp/cc2-camera-reap.status
sha() { sha256sum "$1" | awk '{print $1}'; }
stop() { echo "STOP: $*" >&2; exit 1; }
camera_pid() {
    PID=$(pidof ai_camera 2>/dev/null || true)
    case "$PID" in ''|*' '*|*[!0-9]*) stop 'exactly one ai_camera process required';; esac
}
idle() {
    STATE=$(wget -qO- http://127.0.0.1:8081/api/printer) || stop 'printer telemetry unavailable'
    case "$STATE" in *'"connected":true'*'"machine":{"status":1,'*) ;; *) stop 'connected Idle printer required';; esac
    AGE=$(printf '%s' "$STATE" | sed -n 's/.*"last_message_age":\([0-9][0-9]*\),.*/\1/p')
    case "$AGE" in ''|*[!0-9]*) stop 'missing telemetry age';; esac
    [ "$AGE" -le 15 ] || stop 'stale printer telemetry'
}
runtime_files() {
    [ "$(sha "$1/libcc2-camera-reap-r2.so")" = "$LIB_SHA" ] || return 1
    [ "$(sha "$1/cc2-camera-reap-check")" = "$HELPER_SHA" ] || return 1
}
dependencies() {
    [ -x "$LOADER" ] || return 1
    LD_PRELOAD= CC2_CAMERA_REAP_TRIAL=0 "$LOADER" --list "$CAMERA" > "$1" 2>&1 || return 1
    if grep -q 'not found' "$1"; then return 1; fi
}
