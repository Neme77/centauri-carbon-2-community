#!/bin/sh
# Fixed targets: no environment override for compatibility or filesystem paths.
BASE=/opt/usr/cc2-control/camera
PERSONAL=/opt/usr/cc2-camera-recovery-r2
INIT=/etc/init.d/printer
CAMERA=/opt/bin/ai_camera
LOADER=/lib/ld-linux-armhf.so.3
TRIAL_INIT_SHA=01924b3dc427972838e995ac6763b566b85754a162d60fd6728553a405cd00cc
STOCK_INIT_SHA=bcabb1fa405b066b06c229c6d8cc6e474a0ccc57e31a1b5b387d3b55945be563
INIT_SHA=$TRIAL_INIT_SHA
CAMERA_SHA=ec5633b17de329d5b18c3f8d8de6ebf389391a04193536d976ba167022d07b99
LIB_SHA=8ef3a5c215462e3c9f32c3d4b85ea23e27c41e330d53b22d7cdeca9d100d016a
RECOVERY_SHA=08f2455a99fa5294153ae64ce34117c02f4f8d2dff8527dfd8386aae361a289a
HELPER_SHA=dce1180e3c04512fea1c4f9868b131f3fbe9bda187ebc9342f9db633f0a57ea0
MARKER=/tmp/cc2-camera-reap.status
RECOVERY_MARKER=/tmp/cc2-camera-recovery.status
TRIAL_LINE='( if [ -x /opt/usr/cc2-camera-reap-boot-r1/boot-camera.sh ]; then exec /opt/usr/cc2-camera-reap-boot-r1/boot-camera.sh; else exec /opt/bin/ai_camera; fi ) & # CC2_CAMERA_BOOT_R1'
OLD_LINE=$TRIAL_LINE
NEW_LINE='( if [ -x /opt/usr/cc2-control/camera/boot-camera.sh ]; then exec /opt/usr/cc2-control/camera/boot-camera.sh; else exec /opt/bin/ai_camera; fi ) & # CC2_CAMERA_RECOVERY_R2'
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
    for FILE in libcc2-camera-reap-r2.so libcc2-camera-recovery-r2.so cc2-camera-reap-check; do
        [ -f "$1/$FILE" ] && [ ! -L "$1/$FILE" ] || return 1
    done
    [ "$(sha "$1/libcc2-camera-reap-r2.so")" = "$LIB_SHA" ] || return 1
    [ "$(sha "$1/libcc2-camera-recovery-r2.so")" = "$RECOVERY_SHA" ] || return 1
    [ "$(sha "$1/cc2-camera-reap-check")" = "$HELPER_SHA" ] || return 1
}
dependencies() {
    [ -x "$LOADER" ] || return 1
    LD_PRELOAD= CC2_CAMERA_REAP_TRIAL=0 CC2_CAMERA_RECOVERY_TRIAL=0 "$LOADER" --list "$CAMERA" > "$1" 2>&1 || return 1
    if grep -q 'not found' "$1"; then return 1; fi
}
runtime_check() {
    CC2_CAMERA_REAP_TRIAL=0 CC2_CAMERA_RECOVERY_TRIAL=0 LD_PRELOAD="$1/libcc2-camera-reap-r2.so:$1/libcc2-camera-recovery-r2.so" "$1/cc2-camera-reap-check"
}
enabled() {
    [ -f "$BASE/enabled" ] && [ ! -L "$BASE/enabled" ] && [ "$(cat "$BASE/enabled")" = CAMERA_RECOVERY_R2_ENABLED ]
}

choose_startup() {
    INIT_SHA=$(sha "$INIT")
    case "$INIT_SHA" in
        "$STOCK_INIT_SHA") OLD_LINE='ai_camera &' ;;
        "$TRIAL_INIT_SHA") OLD_LINE=$TRIAL_LINE ;;
        *) stop 'unqualified printer startup; no startup change allowed' ;;
    esac
}
backup_fingerprint() {
    case "$(sha "$1")" in "$STOCK_INIT_SHA"|"$TRIAL_INIT_SHA") return 0;; *) return 1;; esac
}
personal_existing() {
    # A rolled-back personal trial intentionally retains its files. A known
    # native/one-shot startup means that directory is dormant, not active.
    case "$(sha "$INIT")" in "$STOCK_INIT_SHA"|"$TRIAL_INIT_SHA") return 1;; esac
    [ -d "$PERSONAL" ] && [ ! -L "$PERSONAL" ] || return 1
    for item in \
      'common.sh:7aa2ffafba8d49bf657a4b414973410a6eea42e34061c9aefb9df9be30f4fc78' \
      'manage.sh:f4a6c1cfe6dc1b6ab2e936ad1787fba7ea7b71064ce551ddad00e00e0569c4ff' \
      'boot-camera.sh:57b9d994c3b1910bf6409580d588381afbea1d430ec335d7fd7bf86dc23e62ca' \
      'SHA256SUMS:8e983273fc42c0d85adea7719d051a7045cf8a8b0b842766a093ebb5c5791877';do
        file=${item%%:*};expected=${item#*:}
        [ -f "$PERSONAL/$file" ] && [ ! -L "$PERSONAL/$file" ] || stop 'personal camera file missing or symlinked'
        [ "$(sha "$PERSONAL/$file")" = "$expected" ] || stop 'personal camera source mismatch'
    done
    runtime_files "$PERSONAL" || stop 'personal camera runtime mismatch'
    [ -f "$PERSONAL/printer.original" ] && [ ! -L "$PERSONAL/printer.original" ] || stop 'personal startup backup missing'
    [ "$(sha "$PERSONAL/printer.original")" = "$TRIAL_INIT_SHA" ] || stop 'personal startup backup differs'
    personal_line='( if [ -x /opt/usr/cc2-camera-recovery-r2/boot-camera.sh ]; then exec /opt/usr/cc2-camera-recovery-r2/boot-camera.sh; else exec /opt/bin/ai_camera; fi ) & # CC2_CAMERA_RECOVERY_R2'
    expected_init=$(awk -v old="$TRIAL_LINE" -v new="$personal_line" '{line=$0;sub(/^[ \t]*/,"",line);if(line==old){match($0,/^[ \t]*/);print substr($0,1,RLENGTH) new;n++;next}print} END{if(n!=1)exit 1}' "$PERSONAL/printer.original" | sha256sum | awk '{print $1}')
    [ "$(sha "$INIT")" = "$expected_init" ] || stop 'personal camera startup changed'
    [ "$(sha "$CAMERA")" = "$CAMERA_SHA" ] || stop 'camera firmware changed'
    idle
    camera_pid
    [ "$(readlink "/proc/$PID/exe")" = "$CAMERA" ] || stop 'unexpected running camera path'
    [ "$(sha "/proc/$PID/exe")" = "$CAMERA_SHA" ] || stop 'running camera fingerprint differs'
    dependencies /tmp/cc2-camera-update-loader.txt || stop 'camera dependencies unresolved'
    runtime_check "$PERSONAL" || stop 'existing camera runtime check failed'
    echo 'Existing qualified personal R2 installation preserved, including its activation choice.'
    return 0
}
