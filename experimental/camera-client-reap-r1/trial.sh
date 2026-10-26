#!/bin/sh
# Temporary camera-only experiment; never edit /opt/bin or printer startup.
set -eu
umask 077
BASE=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
QUALIFIED=ec5633b17de329d5b18c3f8d8de6ebf389391a04193536d976ba167022d07b99
MARKER=/tmp/cc2-camera-reap.status
LIB="$BASE/libcc2-camera-reap-r2.so"
camera_pid() {
    PID=$(pidof ai_camera 2>/dev/null || true)
    case "$PID" in ''|*' '*) echo 'STOP: exactly one ai_camera process is required.' >&2; return 1;; esac
    case "$PID" in *[!0-9]*) return 1;; esac
}
qualified() {
    REAL=$(readlink "/proc/$PID/exe")
    [ "$REAL" = /opt/bin/ai_camera ] || { echo 'STOP: unexpected camera executable path.' >&2; return 1; }
    for FILE in /opt/bin/ai_camera "/proc/$PID/exe"; do
        HASH=$(sha256sum "$FILE" | awk '{print $1}')
        [ "$HASH" = "$QUALIFIED" ] || { echo 'STOP: unqualified camera executable.' >&2; return 1; }
    done
}
idle() {
    STATE=$(wget -qO- http://127.0.0.1:8081/api/printer)
    case "$STATE" in *'"connected":true'*'"machine":{"status":1,'*) ;; *) echo 'STOP: connected Idle printer required.' >&2; return 1;; esac
    AGE=$(printf '%s' "$STATE" | sed -n 's/.*"last_message_age":\([0-9][0-9]*\),.*/\1/p')
    case "$AGE" in ''|*[!0-9]*) return 1;; esac
    [ "$AGE" -le 15 ] || { echo 'STOP: stale printer state.' >&2; return 1; }
}
check() {
    case "$BASE" in /tmp/cc2-camera-reap-r2) ;; *) echo 'STOP: extract only to /tmp/cc2-camera-reap-r2.' >&2; return 1;; esac
    (cd "$BASE" && sha256sum -c SHA256SUMS)
    CC2_CAMERA_REAP_TRIAL=0 LD_PRELOAD="$LIB" "$BASE/cc2-camera-reap-check"
    camera_pid
    qualified
    idle
}
status() {
    camera_pid
    qualified
    echo "Camera PID: $PID"
    grep -E '^(VmSize:|VmRSS:|Threads:)' "/proc/$PID/status"
    if grep -F "$LIB" "/proc/$PID/maps" >/dev/null 2>&1; then
        echo 'Trial library mapped: yes'
        cat "$MARKER"
    else
        echo 'Trial library mapped: no'
    fi
}
case "${1:-check}" in
check)
    check
    if grep -F "$LIB" "/proc/$PID/maps" >/dev/null 2>&1; then
        echo 'STOP: trial already active; use status.'
        exit 1
    fi
    echo 'CHECK PASS: qualified camera, payload checksums, connected Idle printer. No process restarted.'
    ;;
start)
    check
    if grep -F "$LIB" "/proc/$PID/maps" >/dev/null 2>&1; then
        echo 'STOP: trial already active.'
        exit 1
    fi
    {
        date
        echo "Original camera PID: $PID"
        grep -E '^(VmSize:|VmRSS:|Threads:)' "/proc/$PID/status"
    } > "$BASE/before.txt"
    # Signal only the exact qualified camera process. No printer/callback service restart.
    kill -TERM "$PID"
    COUNT=0
    while kill -0 "$PID" 2>/dev/null; do
        [ "$COUNT" -lt 10 ] || { echo 'STOP: original camera did not exit; no forced kill.' >&2; exit 1; }
        COUNT=$((COUNT+1))
        sleep 1
    done
    sleep 1
    if pidof ai_camera >/dev/null 2>&1; then echo 'STOP: another camera process appeared.' >&2; exit 1; fi
    rm -f "$MARKER"
    CC2_CAMERA_REAP_TRIAL=1 CC2_CAMERA_REAP_ENABLE_LAN=1 LD_PRELOAD="$LIB" /opt/bin/ai_camera > "$BASE/ai_camera-trial.log" 2>&1 &
    NEW_PID=$!
    COUNT=0
    while [ "$COUNT" -lt 15 ]; do
        if ! kill -0 "$NEW_PID" 2>/dev/null; then
            echo 'STOP: trial process exited. Restart the idle printer to restore original startup.' >&2
            tail -n 20 "$BASE/ai_camera-trial.log" >&2 || true
            exit 1
        fi
        if [ -f "$MARKER" ] && grep -F "pid=$NEW_PID patched=1" "$MARKER" >/dev/null; then
            sleep 2
            kill -0 "$NEW_PID" || exit 1
            status
            echo 'TRIAL STARTED: activation only. Streaming, AI and RTC are not yet qualified.'
            echo 'Open the camera normally, observe it, then run status. Do not run a connection stress loop.'
            echo 'Rollback: reboot the printer while idle; the original binary and startup are unchanged.'
            exit 0
        fi
        COUNT=$((COUNT+1))
        sleep 1
    done
    echo 'STOP: activation marker not confirmed. Restart the idle printer to restore original startup.' >&2
    exit 1
    ;;
status)
    status
    ;;
rollback-reboot)
    idle
    echo 'Rebooting idle printer. Original camera startup will be restored; /tmp trial is not persistent.'
    reboot
    ;;
*)
    echo 'Usage: sh trial.sh check | start | status | rollback-reboot' >&2
    exit 2
    ;;
esac
