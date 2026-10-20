#!/bin/sh
set -eu
umask 077
export LD_LIBRARY_PATH=/opt/lib
SOURCE=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
. "$SOURCE/common.sh"
ROOT_STAGE=''
LOCKED=0
cleanup() {
    if [ -n "$ROOT_STAGE" ]; then
        rm -f "$ROOT_STAGE/printer" "$ROOT_STAGE/generated"
        rmdir "$ROOT_STAGE" 2>/dev/null || true
    fi
    if [ "$LOCKED" -eq 1 ]; then rmdir "$BASE.lock" 2>/dev/null || true; fi
}
trap cleanup 0
trap 'exit 1' 1 2 15
lock() {
    mkdir "$BASE.lock" || stop 'another staging/rollback operation or retained lock exists'
    LOCKED=1
}
source_check() {
    [ "$(id -u)" = 0 ] || stop 'root required'
    [ -f "$INIT" ] && [ ! -L "$INIT" ] || stop 'printer startup must be a regular file'
    [ "$(sha "$INIT")" = "$INIT_SHA" ] || stop 'printer startup fingerprint differs; no file changed'
    [ "$(sha "$CAMERA")" = "$CAMERA_SHA" ] || stop 'camera fingerprint differs'
    (cd "$SOURCE" && sha256sum -c SHA256SUMS) || stop 'package manifest mismatch'
    runtime_files "$SOURCE" || stop 'R2 runtime fingerprint mismatch'
    for F in common.sh manage.sh boot-camera.sh; do sh -n "$SOURCE/$F" || stop 'shell syntax mismatch'; done
    [ "$(grep -c '^[[:space:]]*ai_camera[[:space:]]*&[[:space:]]*$' "$INIT" || true)" = 1 ] || stop 'expected one native camera launch line'
    idle
    camera_pid
    [ "$(readlink "/proc/$PID/exe")" = "$CAMERA" ] || stop 'unexpected running camera path'
    [ "$(sha "/proc/$PID/exe")" = "$CAMERA_SHA" ] || stop 'running camera fingerprint differs'
    if grep -F 'libcc2-camera-reap' "/proc/$PID/maps" >/dev/null 2>&1; then stop 'camera trial already active; restore original boot first'; fi
    dependencies /tmp/cc2-camera-boot-loader.txt || stop 'camera dependencies unresolved'
    CC2_CAMERA_REAP_TRIAL=0 LD_PRELOAD="$SOURCE/libcc2-camera-reap-r2.so" "$SOURCE/cc2-camera-reap-check" || stop 'runtime symbol preflight failed'
}
root_stage() {
    ROOT_STAGE=/etc/init.d/.cc2-camera-stage-$$
    mkdir "$ROOT_STAGE" || stop 'cannot stage an atomic startup replacement'
}
stage() {
    lock
    source_check
    [ ! -e "$BASE" ] && [ ! -L "$BASE" ] || stop 'boot trial directory already exists; use its rollback/status, do not overwrite'
    root_stage
    cp -p "$INIT" "$ROOT_STAGE/printer"
    # One launch line only; missing persistent storage still executes the native camera.
    awk '/^[ \t]*ai_camera[ \t]*&[ \t]*$/ {
        match($0,/^[ \t]*/)
        print substr($0,1,RLENGTH) "( if [ -x /opt/usr/cc2-camera-reap-boot-r1/boot-camera.sh ]; then exec /opt/usr/cc2-camera-reap-boot-r1/boot-camera.sh; else exec /opt/bin/ai_camera; fi ) & # CC2_CAMERA_BOOT_R1"
        n++; next
    } { print } END { if(n!=1) exit 1 }' "$INIT" > "$ROOT_STAGE/generated" || stop 'startup transformation refused'
    cat "$ROOT_STAGE/generated" > "$ROOT_STAGE/printer"
    sh -n "$ROOT_STAGE/printer" || stop 'generated startup syntax failed'
    [ "$(grep -c '# CC2_CAMERA_BOOT_R1$' "$ROOT_STAGE/printer")" = 1 ] || stop 'generated hook mismatch'
    mkdir "$BASE"
    chmod 700 "$BASE"
    for F in common.sh manage.sh boot-camera.sh libcc2-camera-reap-r2.so cc2-camera-reap-check SHA256SUMS; do cp "$SOURCE/$F" "$BASE/$F"; done
    chmod 700 "$BASE/boot-camera.sh" "$BASE/cc2-camera-reap-check" "$BASE/manage.sh"
    cp -p "$INIT" "$BASE/printer.original"
    [ "$(sha "$BASE/printer.original")" = "$INIT_SHA" ] || stop 'startup backup verification failed'
    sha "$ROOT_STAGE/printer" > "$BASE/patched.sha256"
    printf 'ONE_BOOT_ONLY\n' > "$BASE/armed"
    (cd "$BASE" && sha256sum -c SHA256SUMS >/dev/null) || stop 'staged payload verification failed'
    # Revalidate immediately before the only system-file write.
    idle
    [ "$(sha "$INIT")" = "$INIT_SHA" ] || stop 'startup changed during staging'
    sync
    mv "$ROOT_STAGE/printer" "$INIT"
    sync
    [ "$(sha "$INIT")" = "$(cat "$BASE/patched.sha256")" ] || stop 'committed startup verification failed; inspect backup'
    echo 'STAGED: no process restarted. Next normal idle-printer reboot will try R2 once, with forced LAN disabled.'
    echo 'Another printer reboot returns to original camera startup through the unarmed wrapper.'
    echo "Exact startup-file rollback: sh $BASE/manage.sh rollback"
}
rollback() {
    lock
    idle
    [ -f "$BASE/printer.original" ] && [ ! -L "$BASE/printer.original" ] || stop 'original startup backup missing'
    [ "$(sha "$BASE/printer.original")" = "$INIT_SHA" ] || stop 'backup fingerprint differs'
    CURRENT=$(sha "$INIT")
    if [ "$CURRENT" = "$INIT_SHA" ]; then
        rm -f "$BASE/armed"
        echo 'Original startup already restored. Current process remains unchanged.'
        return
    fi
    [ "$CURRENT" = "$(cat "$BASE/patched.sha256")" ] || stop 'startup was changed by something else; refusing to overwrite it'
    root_stage
    cp -p "$BASE/printer.original" "$ROOT_STAGE/printer"
    sh -n "$ROOT_STAGE/printer" || stop 'backup syntax failed'
    [ "$(sha "$ROOT_STAGE/printer")" = "$INIT_SHA" ] || stop 'restore copy mismatch'
    [ "$(sha "$INIT")" = "$CURRENT" ] || stop 'startup changed during rollback'
    mv "$ROOT_STAGE/printer" "$INIT"
    rm -f "$BASE/armed"
    sync
    [ "$(sha "$INIT")" = "$INIT_SHA" ] || stop 'restored startup verification failed'
    echo 'ORIGINAL STARTUP RESTORED. No service restarted; current R2 process, if any, lasts until an idle-printer reboot.'
    echo 'Backup and trial payload retained for inspection.'
}
status() {
    camera_pid
    [ "$(sha "/proc/$PID/exe")" = "$CAMERA_SHA" ] || stop 'unqualified running camera'
    echo "Camera PID: $PID"
    grep -E '^(State:|VmSize:|VmRSS:|Threads:)' "/proc/$PID/status"
    if grep -F "$BASE/libcc2-camera-reap-r2.so" "/proc/$PID/maps" >/dev/null 2>&1; then
        echo 'Boot R2 library mapped: yes'
        [ -r "$MARKER" ] || stop 'activation marker missing'
        grep -F "pid=$PID patched=1 enable_lan_once=0 " "$MARKER" || stop 'current activation/forced-LAN state not verified'
    else
        echo 'Boot R2 library mapped: no'
    fi
    if [ -f "$BASE/armed" ]; then echo 'Next boot: armed for one trial'; else echo 'Next boot: original camera (trial unarmed)'; fi
    [ ! -f "$BASE/last-attempt.txt" ] || cat "$BASE/last-attempt.txt"
    echo 'Process/marker status does not prove local video, Matrix/RTC or AI.'
}
case "${1:-check}" in
check) source_check; echo 'CHECK PASS: pinned startup/camera/runtime, fresh Idle state and dependencies. No file installed and no process restarted.' ;;
stage) stage ;;
rollback) rollback ;;
status) status ;;
*) echo 'Usage: sh manage.sh check | stage | status | rollback' >&2; exit 2 ;;
esac
