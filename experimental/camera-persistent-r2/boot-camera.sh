#!/bin/sh
# Original camera launch position, persistent opt-in. No heating or motion.
set -eu
umask 077
BASE=/opt/usr/cc2-camera-recovery-r2
CAMERA=/opt/bin/ai_camera
export LD_LIBRARY_PATH=/opt/lib
original() {
    printf 'mode=native reason=%s pid=%s\n' "$1" "$$" > "$BASE/last-attempt.txt" 2>/dev/null || true
    shift
    CC2_CAMERA_REAP_TRIAL=0 CC2_CAMERA_REAP_ENABLE_LAN=0 CC2_CAMERA_RECOVERY_TRIAL=0 exec "$CAMERA" "$@"
}
if pidof ai_camera >/dev/null 2>&1;then
    echo 'CC2 camera: process already exists; no duplicate started.' >&2
    exit 1
fi
[ -d "$BASE" ] && [ ! -L "$BASE" ] || original missing-storage "$@"
[ -f "$BASE/enabled" ] && [ ! -L "$BASE/enabled" ] || original disabled "$@"
[ "$(cat "$BASE/enabled")" = CAMERA_RECOVERY_R2_ENABLED ] || original invalid-enable-marker "$@"
[ -z "${LD_PRELOAD:-}" ] || original existing-preload "$@"
for FILE in common.sh manage.sh boot-camera.sh SHA256SUMS;do
    [ -f "$BASE/$FILE" ] && [ ! -L "$BASE/$FILE" ] || original missing-payload "$@"
done
(cd "$BASE" && sha256sum -c SHA256SUMS >/dev/null 2>&1) || original payload-mismatch "$@"
. "$BASE/common.sh"
[ "$(sha "$CAMERA")" = "$CAMERA_SHA" ] || original camera-firmware-changed "$@"
[ -f "$INIT" ] && [ ! -L "$INIT" ] || original startup-not-regular "$@"
[ -f "$BASE/patched.sha256" ] && [ ! -L "$BASE/patched.sha256" ] || original missing-startup-fingerprint "$@"
[ "$(sha "$INIT")" = "$(cat "$BASE/patched.sha256")" ] || original startup-changed "$@"
runtime_files "$BASE" || original runtime-mismatch "$@"
dependencies "$BASE/loader-status.txt" || original dependencies-unavailable "$@"
runtime_check "$BASE" > "$BASE/runtime-check.txt" 2>&1 || original runtime-preflight-failed "$@"
rm -f "$MARKER" "$RECOVERY_MARKER"
printf 'mode=patched reap=R2 recovery=R2 persistent=1 forced_lan=1 pid=%s\n' "$$" > "$BASE/last-attempt.txt"
CC2_CAMERA_REAP_TRIAL=1 CC2_CAMERA_REAP_ENABLE_LAN=1 CC2_CAMERA_RECOVERY_TRIAL=1 LD_PRELOAD="$BASE/libcc2-camera-reap-r2.so:$BASE/libcc2-camera-recovery-r2.so" exec "$CAMERA" "$@"
