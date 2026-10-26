#!/bin/sh
# Temporary camera-only trial. No firmware, printer executable or startup edits.
set -eu
umask 077
export LD_LIBRARY_PATH=/opt/lib
BASE=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
CAMERA=/opt/bin/ai_camera
QUALIFIED=ec5633b17de329d5b18c3f8d8de6ebf389391a04193536d976ba167022d07b99
REAP_SHA=8ef3a5c215462e3c9f32c3d4b85ea23e27c41e330d53b22d7cdeca9d100d016a
HELPER_SHA=dce1180e3c04512fea1c4f9868b131f3fbe9bda187ebc9342f9db633f0a57ea0
stop(){ echo "STOP: $*" >&2; exit 1; }
sha(){ sha256sum "$1" | awk '{print $1}'; }
camera_pid(){
    PID=$(pidof ai_camera 2>/dev/null || true)
    case "$PID" in ''|*' '*|*[!0-9]*) stop 'exactly one camera required';; esac
}
qualified(){
    [ "$(readlink "/proc/$PID/exe")" = "$CAMERA" ] || stop 'unexpected camera path'
    [ "$(sha "$CAMERA")" = "$QUALIFIED" ] || stop 'camera file differs'
    [ "$(sha "/proc/$PID/exe")" = "$QUALIFIED" ] || stop 'running camera differs'
}
idle(){
    STATE=$(wget -qO- http://127.0.0.1:8081/api/printer) || stop 'telemetry unavailable'
    case "$STATE" in *'"connected":true'*'"machine":{"status":1,'*) ;; *) stop 'connected Idle printer required';; esac
    AGE=$(printf '%s' "$STATE" | sed -n 's/.*"last_message_age":\([0-9][0-9]*\),.*/\1/p')
    case "$AGE" in ''|*[!0-9]*) stop 'missing telemetry age';; esac
    [ "$AGE" -le 15 ] || stop 'stale telemetry'
}
check(){
    [ "$(id -u)" = 0 ] || stop 'root required'
    [ "$BASE" = /tmp/cc2-camera-recovery-r2 ] || stop 'extract to /tmp/cc2-camera-recovery-r2'
    (cd "$BASE" && sha256sum -c SHA256SUMS) || stop 'manifest mismatch'
    [ "$(sha "$BASE/libcc2-camera-reap-r2.so")" = "$REAP_SHA" ] || stop 'qualified R2 differs'
    [ "$(sha "$BASE/cc2-camera-reap-check")" = "$HELPER_SHA" ] || stop 'qualified runtime helper differs'
    camera_pid;qualified;idle
    if grep -F 'libcc2-camera-recovery-r1.so' "/proc/$PID/maps" >/dev/null;then
        stop 'previous recovery R1 still active; reboot idle printer and verify original video before this trial'
    fi
    LD_PRELOAD= /lib/ld-linux-armhf.so.3 --list "$CAMERA" > "$BASE/loader.txt" 2>&1 || stop 'loader failed'
    if grep -q 'not found' "$BASE/loader.txt";then stop 'missing dependency';fi
    CC2_CAMERA_REAP_TRIAL=0 CC2_CAMERA_RECOVERY_TRIAL=0 LD_PRELOAD="$BASE/libcc2-camera-reap-r2.so:$BASE/libcc2-camera-recovery-r2.so" "$BASE/cc2-camera-reap-check" || stop 'runtime preflight failed'
}
status(){
    camera_pid;qualified
    echo "Camera PID: $PID"
    grep -E '^(State:|VmSize:|VmRSS:|Threads:)' "/proc/$PID/status"
    for LIB in libcc2-camera-reap-r2.so libcc2-camera-recovery-r2.so;do
        if grep -F "$BASE/$LIB" "/proc/$PID/maps" >/dev/null;then echo "$LIB: mapped";else echo "$LIB: not mapped";fi
    done
    for FILE in /tmp/cc2-camera-reap.status /tmp/cc2-camera-recovery.status;do
        if [ -f "$FILE" ] && grep -F "pid=$PID " "$FILE" >/dev/null;then cat "$FILE";fi
    done
    echo 'Markers confirm activation/notifications only. Verify local and Matrix video yourself.'
}
case "${1:-check}" in
check) check;echo 'CHECK PASS: no file installed, process restarted or notification sent.';;
start|restart)
    check
    if [ "$1" = start ] && grep -F "$BASE/libcc2-camera-recovery-r2.so" "/proc/$PID/maps" >/dev/null;then stop 'trial already active; use status or restart';fi
    if [ "$1" = restart ] && ! grep -F "$BASE/libcc2-camera-recovery-r2.so" "/proc/$PID/maps" >/dev/null;then stop 'restart requires this trial already active';fi
    PRINTER_PIDS=$(pidof elegoo_printer)
    echo "Stopping only qualified camera PID $PID. Printer PID: $PRINTER_PIDS"
    kill -TERM "$PID"
    COUNT=0
    while kill -0 "$PID" 2>/dev/null;do
        [ "$COUNT" -lt 15 ] || stop 'camera did not exit; no forced kill'
        COUNT=$((COUNT+1));sleep 1
    done
    sleep 1
    if pidof ai_camera >/dev/null 2>&1;then stop 'another camera appeared';fi
    rm -f /tmp/cc2-camera-reap.status /tmp/cc2-camera-recovery.status
    CC2_CAMERA_REAP_TRIAL=1 CC2_CAMERA_REAP_ENABLE_LAN=1 CC2_CAMERA_RECOVERY_TRIAL=1 LD_PRELOAD="$BASE/libcc2-camera-reap-r2.so:$BASE/libcc2-camera-recovery-r2.so" "$CAMERA" > "$BASE/ai_camera-trial.log" 2>&1 &
    NEW_PID=$!
    COUNT=0
    while [ "$COUNT" -lt 15 ];do
        kill -0 "$NEW_PID" 2>/dev/null || stop 'trial camera exited; reboot idle printer to restore existing startup'
        if [ -f /tmp/cc2-camera-recovery.status ] && grep -F "pid=$NEW_PID patched=1" /tmp/cc2-camera-recovery.status >/dev/null;then
            sleep 3
            [ "$(pidof elegoo_printer)" = "$PRINTER_PIDS" ] || stop 'printer process changed unexpectedly'
            status
            echo 'TRIAL STARTED. Test local video and Matrix. No automatic reboot or persistent install.'
            exit 0
        fi
        COUNT=$((COUNT+1));sleep 1
    done
    stop 'activation not confirmed; reboot idle printer to restore existing startup';;
status) status;;
*) echo 'Usage: sh trial.sh check | start | status | restart' >&2;exit 2;;
esac
