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
    choose_startup
    [ "$(sha "$CAMERA")" = "$CAMERA_SHA" ] || stop 'camera fingerprint differs'
    (cd "$SOURCE" && sha256sum -c SHA256SUMS) || stop 'package manifest mismatch'
    runtime_files "$SOURCE" || stop 'R2 runtime fingerprint mismatch'
    for F in common.sh manage.sh boot-camera.sh; do sh -n "$SOURCE/$F" || stop 'shell syntax mismatch'; done
    COUNT=$(awk -v old="$OLD_LINE" '{line=$0;sub(/^[ \t]*/,"",line);if(line==old)n++} END{print n+0}' "$INIT")
    [ "$COUNT" = 1 ] || stop 'expected the exact previous one-boot camera line'
    [ ! -e /opt/usr/cc2-camera-reap-boot-r1/armed ] || stop 'previous one-boot trial is still armed; do not overwrite its intention'
    idle
    camera_pid
    [ "$(readlink "/proc/$PID/exe")" = "$CAMERA" ] || stop 'unexpected running camera path'
    [ "$(sha "/proc/$PID/exe")" = "$CAMERA_SHA" ] || stop 'running camera fingerprint differs'
    dependencies /tmp/cc2-camera-boot-loader.txt || stop 'camera dependencies unresolved'
    runtime_check "$SOURCE" || stop 'runtime symbol preflight failed'
}
root_stage() {
    ROOT_STAGE=/etc/init.d/.cc2-camera-stage-$$
    mkdir "$ROOT_STAGE" || stop 'cannot stage an atomic startup replacement'
}
dormant_check() {
    # A qualified native/one-shot startup means these retained files are not
    # installed into startup. Authenticate them before retaining/moving them.
    choose_startup
    [ -d "$BASE" ] && [ ! -L "$BASE" ] || stop 'retained camera directory must be regular'
    for F in common.sh manage.sh boot-camera.sh SHA256SUMS printer.original patched.sha256 libcc2-camera-reap-r2.so libcc2-camera-recovery-r2.so cc2-camera-reap-check;do
        [ -f "$BASE/$F" ] && [ ! -L "$BASE/$F" ] || stop 'retained camera file missing or symlinked'
    done
    (cd "$BASE" && sha256sum -c SHA256SUMS) || stop 'retained camera checksums differ'
    runtime_files "$BASE" || stop 'retained camera runtime differs'
    for ITEM in 'common.sh:f6dc57c5e54a1763f0c60c79c75362da604fe5350857958618b6be3cb071ef8e' 'manage.sh:578493eae89ff602a7c8ab1bf65bfb3c71298ff45d5fd7eb419d8da20f07c487' 'boot-camera.sh:e570b84e4b5615bfea53be5c52a1befef7b78f0b91ba16e62b500652a8cdfaa5';do
        FILE=${ITEM%%:*};PIN=${ITEM#*:}
        ACTUAL=$(sha "$BASE/$FILE")
        [ "$ACTUAL" = "$PIN" ] || [ "$ACTUAL" = "$(sha "$SOURCE/$FILE")" ] || stop 'unknown retained camera source'
    done
    backup_fingerprint "$BASE/printer.original" || stop 'retained startup backup is unqualified'
    PREVIOUS_LINE='ai_camera &'
    [ "$(sha "$BASE/printer.original")" != "$TRIAL_INIT_SHA" ] || PREVIOUS_LINE=$TRIAL_LINE
    PREVIOUS=$(awk -v old="$PREVIOUS_LINE" -v new="$NEW_LINE" '{line=$0;sub(/^[ \t]*/,"",line);if(line==old){match($0,/^[ \t]*/);print substr($0,1,RLENGTH) new;n++;next}print} END{if(n!=1)exit 1}' "$BASE/printer.original" | sha256sum | awk '{print $1}')
    FIRMWARE=$(awk '{line=$0;sub(/^[ \t]*/,"",line);if(line=="ai_camera &"){match($0,/^[ \t]*/);print substr($0,1,RLENGTH) "/etc/init.d/cc2-camera-launch & # CC2_CAMERA_FIRMWARE_R2";n++;next}print} END{if(n!=1)exit 1}' "$BASE/printer.original" | sha256sum | awk '{print $1}')
    SAVED=$(cat "$BASE/patched.sha256")
    [ "$SAVED" = "$PREVIOUS" ] || { [ "$(sha "$BASE/printer.original")" = "$STOCK_INIT_SHA" ] && [ "$SAVED" = "$FIRMWARE" ]; } || stop 'retained startup fingerprint differs'
}
stage() {
    lock
    DORMANT=0
    if [ -e "$BASE" ] || [ -L "$BASE" ]; then
        case "$(sha "$INIT")" in
            "$STOCK_INIT_SHA"|"$TRIAL_INIT_SHA") dormant_check; DORMANT=1 ;;
            *) idle; installed_check; echo 'ALREADY INSTALLED: existing camera activation preserved. Reboot required if not yet active.'; return ;;
        esac
    fi
    if personal_existing; then return; fi
    source_check
    if [ "$DORMANT" -eq 1 ];then
        RETAINED=/opt/usr/cc2-camera-before-reinstall-$$
        [ ! -e "$RETAINED" ] && [ ! -L "$RETAINED" ] || stop 'retained backup already exists'
        [ "$(sha "$INIT")" = "$INIT_SHA" ] || stop 'startup changed before retained-directory move'
        mv "$BASE" "$RETAINED" || stop 'cannot preserve previous camera directory'
        [ "$SOURCE" != "$BASE" ] || SOURCE=$RETAINED
        echo "Previous dormant camera files retained: $RETAINED"
    fi
    [ ! -e "$BASE" ] && [ ! -L "$BASE" ] || stop 'persistent directory already exists; use status/disable/rollback, do not overwrite'
    root_stage
    cp -p "$INIT" "$ROOT_STAGE/printer"
    # One launch line only; missing persistent storage still executes the native camera.
    awk -v old="$OLD_LINE" -v new="$NEW_LINE" '{
        line=$0;sub(/^[ \t]*/,"",line)
        if(line==old){match($0,/^[ \t]*/);print substr($0,1,RLENGTH) new;n++;next}
        print
    } END {if(n!=1)exit 1}' "$INIT" > "$ROOT_STAGE/generated" || stop 'startup transformation refused'
    cat "$ROOT_STAGE/generated" > "$ROOT_STAGE/printer"
    sh -n "$ROOT_STAGE/printer" || stop 'generated startup syntax failed'
    [ "$(grep -c '# CC2_CAMERA_RECOVERY_R2$' "$ROOT_STAGE/printer")" = 1 ] || stop 'generated hook mismatch'
    mkdir "$BASE"
    chmod 700 "$BASE"
    for F in common.sh manage.sh boot-camera.sh libcc2-camera-reap-r2.so libcc2-camera-recovery-r2.so cc2-camera-reap-check SHA256SUMS; do cp "$SOURCE/$F" "$BASE/$F"; done
    chmod 700 "$BASE/boot-camera.sh" "$BASE/cc2-camera-reap-check" "$BASE/manage.sh"
    cp -p "$INIT" "$BASE/printer.original"
    [ "$(sha "$BASE/printer.original")" = "$INIT_SHA" ] || stop 'startup backup verification failed'
    sha "$ROOT_STAGE/printer" > "$BASE/patched.sha256"
    printf 'CAMERA_RECOVERY_R2_ENABLED\n' > "$BASE/enabled"
    (cd "$BASE" && sha256sum -c SHA256SUMS >/dev/null) || stop 'staged payload verification failed'
    # Revalidate immediately before the only system-file write.
    idle
    [ "$(sha "$INIT")" = "$INIT_SHA" ] || stop 'startup changed during staging'
    sync
    mv "$ROOT_STAGE/printer" "$INIT"
    sync
    [ "$(sha "$INIT")" = "$(cat "$BASE/patched.sha256")" ] || stop 'committed startup verification failed; inspect backup'
    echo 'INSTALLED: no process restarted. Next normal idle-printer reboot will load both qualified R2 libraries.'
    echo 'Persistent activation remains enabled across reboots. Preflight refusal falls back to the native camera.'
    echo "Exact startup-file rollback: sh $BASE/manage.sh rollback"
}
rollback() {
    lock
    idle
    [ -f "$INIT" ] && [ ! -L "$INIT" ] || stop 'startup must be a regular file'
    [ -f "$BASE/printer.original" ] && [ ! -L "$BASE/printer.original" ] || stop 'original startup backup missing'
    backup_fingerprint "$BASE/printer.original" || stop 'backup fingerprint differs'
    INIT_SHA=$(sha "$BASE/printer.original")
    CURRENT=$(sha "$INIT")
    if [ "$CURRENT" = "$INIT_SHA" ]; then
        rm -f "$BASE/enabled"
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
    rm -f "$BASE/enabled"
    sync
    [ "$(sha "$INIT")" = "$INIT_SHA" ] || stop 'restored startup verification failed'
    echo 'PREVIOUS STARTUP RESTORED. No service restarted; current R2 process, if any, lasts until an idle-printer reboot.'
    echo 'Backup and trial payload retained for inspection.'
}
status() {
    camera_pid
    [ "$(sha "/proc/$PID/exe")" = "$CAMERA_SHA" ] || stop 'unqualified running camera'
    echo "Camera PID: $PID"
    grep -E '^(State:|VmSize:|VmRSS:|Threads:)' "/proc/$PID/status"
    for LIB in libcc2-camera-reap-r2.so libcc2-camera-recovery-r2.so; do
        if grep -F "$BASE/$LIB" "/proc/$PID/maps" >/dev/null 2>&1;then echo "$LIB: mapped from persistent directory";else echo "$LIB: not mapped from persistent directory";fi
    done
    for FILE in "$MARKER" "$RECOVERY_MARKER";do
        if [ -r "$FILE" ];then grep -F "pid=$PID patched=1 " "$FILE" || true;fi
    done
    if enabled;then echo 'Next boot: persistent R2 enabled (subject to preflight)';else echo 'Next boot: native camera (persistent R2 disabled)';fi
    [ ! -f "$BASE/last-attempt.txt" ] || cat "$BASE/last-attempt.txt"
    echo 'Process/marker status does not prove local video, Matrix/RTC or AI.'
}
installed_check() {
    [ "$(id -u)" = 0 ] || stop 'root required'
    [ -f "$INIT" ] && [ ! -L "$INIT" ] || stop 'startup must be regular'
    [ -d "$BASE" ] && [ ! -L "$BASE" ] || stop 'installed directory must be regular'
    [ -r "$BASE/patched.sha256" ] && [ ! -L "$BASE/patched.sha256" ] || stop 'installed startup fingerprint missing'
    [ "$(sha "$INIT")" = "$(cat "$BASE/patched.sha256")" ] || stop 'startup changed; refusing to modify configuration'
    (cd "$BASE" && sha256sum -c SHA256SUMS) || stop 'installed payload mismatch'
    runtime_files "$BASE" || stop 'installed R2 runtime differs'
    [ "$(sha "$CAMERA")" = "$CAMERA_SHA" ] || stop 'camera firmware changed'
}
toggle() {
    lock
    idle
    if [ "$1" = enable ];then
        installed_check
        printf 'CAMERA_RECOVERY_R2_ENABLED\n' > "$BASE/enabled.new"
        mv "$BASE/enabled.new" "$BASE/enabled"
        echo 'ENABLED for the next camera start. Current process unchanged.'
    else
        [ -d "$BASE" ] && [ ! -L "$BASE" ] || stop 'persistent installation missing'
        rm -f "$BASE/enabled"
        echo 'DISABLED for the next camera start. Reboot while idle to return to native startup.'
    fi
}
case "${1:-check}" in
check) if personal_existing; then :; elif [ -e "$BASE" ] || [ -L "$BASE" ]; then case "$(sha "$INIT")" in "$STOCK_INIT_SHA"|"$TRIAL_INIT_SHA") dormant_check; source_check;; *) idle; installed_check;; esac; else source_check; fi; echo 'CHECK PASS: pinned startup/camera/runtime, fresh Idle state and dependencies. No file installed and no process restarted.' ;;
install) stage ;;
rollback) rollback ;;
status) status ;;
enable|disable) toggle "$1" ;;
verify) installed_check; echo 'INSTALLED FILES VERIFIED; current video still requires observation.' ;;
*) echo 'Usage: sh manage.sh check | install | status | verify | disable | enable | rollback' >&2; exit 2 ;;
esac

