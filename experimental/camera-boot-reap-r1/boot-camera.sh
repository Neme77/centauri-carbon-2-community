#!/bin/sh
# Called at the original camera launch position; no restarts, heating or motion.
set -eu
umask 077
BASE=/opt/usr/cc2-camera-reap-boot-r1
CAMERA=/opt/bin/ai_camera
export LD_LIBRARY_PATH=/opt/lib
original() {
    echo 'CC2 camera: original startup (trial not armed or preflight refused).' >&2
    CC2_CAMERA_REAP_TRIAL=0 CC2_CAMERA_REAP_ENABLE_LAN=0 exec "$CAMERA" "$@"
}
[ ! -L "$BASE/armed" ] || original "$@"
[ -f "$BASE/armed" ] || original "$@"
[ "$(cat "$BASE/armed")" = ONE_BOOT_ONLY ] || original "$@"
# Consume before any activation: another printer reboot returns to the original.
mv "$BASE/armed" "$BASE/consumed" || original "$@"
printf 'one_shot=consumed forced_lan=0 pid=%s\n' "$$" > "$BASE/last-attempt.txt"
(cd "$BASE" && sha256sum -c SHA256SUMS >/dev/null 2>&1) || original "$@"
[ -r "$BASE/common.sh" ] || original "$@"
. "$BASE/common.sh"
if pidof ai_camera >/dev/null 2>&1; then
    echo 'CC2 camera: another camera process exists; no duplicate started.' >&2
    exit 1
fi
[ -z "${LD_PRELOAD:-}" ] || original "$@"
[ "$(sha "$CAMERA")" = "$CAMERA_SHA" ] || original "$@"
[ -r "$BASE/patched.sha256" ] || original "$@"
[ "$(sha "$INIT")" = "$(cat "$BASE/patched.sha256")" ] || original "$@"
runtime_files "$BASE" || original "$@"
dependencies /tmp/cc2-camera-boot-loader.txt || original "$@"
CC2_CAMERA_REAP_TRIAL=0 LD_PRELOAD="$BASE/libcc2-camera-reap-r2.so" "$BASE/cc2-camera-reap-check" > "$BASE/runtime-check.txt" 2>&1 || original "$@"
rm -f "$MARKER"
echo 'CC2 camera: one-boot R2 reclamation; native LAN/RTC initialization retained.' >&2
CC2_CAMERA_REAP_TRIAL=1 CC2_CAMERA_REAP_ENABLE_LAN=0 LD_PRELOAD="$BASE/libcc2-camera-reap-r2.so" exec "$CAMERA" "$@"
