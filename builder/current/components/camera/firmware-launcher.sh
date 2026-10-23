#!/bin/sh
# Run at the original vendor camera position; first-boot seed is atomic.
set -eu
umask 077
export LD_LIBRARY_PATH=/opt/lib
SOURCE=/opt/inst/cc2-camera-r2
BASE=/opt/usr/cc2-control/camera
CAMERA=/opt/bin/ai_camera
INIT=/etc/init.d/printer
LOCKED=0
STAGE=''
cleanup() {
    if [ -n "$STAGE" ] && [ -d "$STAGE" ];then
        rm -f "$STAGE/"*
        rmdir "$STAGE" 2>/dev/null || true
    fi
    [ "$LOCKED" -eq 0 ] || rmdir "$BASE.lock" 2>/dev/null || true
}
trap cleanup 0
trap 'exit 1' 1 2 15
native() {
    cleanup
    trap - 0 1 2 15
    printf 'mode=native reason=%s pid=%s\n' "$1" "$$" > /tmp/cc2-camera-firmware-status.txt
    logger -t cc2-camera "R2 firmware preflight refused: $1; using native camera"
    shift
    CC2_CAMERA_REAP_TRIAL=0 CC2_CAMERA_REAP_ENABLE_LAN=0 CC2_CAMERA_RECOVERY_TRIAL=0 exec "$CAMERA" "$@"
}
if pidof ai_camera >/dev/null 2>&1;then exit 1;fi
[ -z "${LD_PRELOAD:-}" ] || native existing-preload "$@"
grep -q ' /opt/usr ' /proc/mounts || native persistent-storage-unmounted "$@"
for item in "$SOURCE" "$SOURCE/printer.original" "$SOURCE/patched.sha256" "$INIT" "$CAMERA";do
    [ -e "$item" ] && [ ! -L "$item" ] || native missing-or-symlinked-input "$@"
done
[ "$(sha256sum "$CAMERA" | awk '{print $1}')" = ec5633b17de329d5b18c3f8d8de6ebf389391a04193536d976ba167022d07b99 ] || native camera-fingerprint "$@"
[ "$(sha256sum "$SOURCE/printer.original" | awk '{print $1}')" = bcabb1fa405b066b06c229c6d8cc6e474a0ccc57e31a1b5b387d3b55945be563 ] || native original-startup "$@"
[ "$(sha256sum "$INIT" | awk '{print $1}')" = "$(cat "$SOURCE/patched.sha256")" ] || native startup-fingerprint "$@"
(cd "$SOURCE" && sha256sum -c SHA256SUMS >/dev/null 2>&1) || native source-checksums "$@"
. "$SOURCE/common.sh"
runtime_files "$SOURCE" || native source-runtime "$@"
dependencies /tmp/cc2-camera-firmware-loader.txt || native dependencies "$@"
runtime_check "$SOURCE" >/tmp/cc2-camera-firmware-runtime.txt 2>&1 || native runtime-preflight "$@"
[ ! -L /opt/usr/cc2-control ] || native app-storage-symlink "$@"
mkdir -p /opt/usr/cc2-control || native app-storage "$@"
mkdir "$BASE.lock" || native component-locked "$@"
LOCKED=1
DISABLED=0
if [ -e "$BASE" ] || [ -L "$BASE" ];then
    [ -d "$BASE" ] && [ ! -L "$BASE" ] || native existing-component-type "$@"
    for file in common.sh manage.sh boot-camera.sh SHA256SUMS libcc2-camera-reap-r2.so libcc2-camera-recovery-r2.so cc2-camera-reap-check;do
        [ -f "$BASE/$file" ] && [ ! -L "$BASE/$file" ] || native existing-component-file "$@"
        if ! cmp -s "$SOURCE/$file" "$BASE/$file";then
            case "$file:$(sha "$BASE/$file")" in
                manage.sh:578493eae89ff602a7c8ab1bf65bfb3c71298ff45d5fd7eb419d8da20f07c487|SHA256SUMS:581c066726993e6d69d83303a1f14190c11ef29862052f1b84b3c6f12f800ab3) ;;
                *) native unknown-existing-component "$@" ;;
            esac
        fi
    done
    [ ! -L "$BASE/enabled" ] && [ ! -L "$BASE/patched.sha256" ] && [ ! -L "$BASE/printer.original" ] || native existing-marker-symlink "$@"
    if [ ! -e "$BASE/enabled" ];then DISABLED=1
    elif [ "$(cat "$BASE/enabled")" != CAMERA_RECOVERY_R2_ENABLED ];then native invalid-enabled-marker "$@";fi
    CURRENT=$(cat "$BASE/patched.sha256" 2>/dev/null || true)
    EXPECTED=$(cat "$SOURCE/patched.sha256")
    if [ "$CURRENT" != "$EXPECTED" ];then
        # Accept only the exact updater-managed native/one-shot predecessors.
        [ -f "$BASE/printer.original" ] && backup_fingerprint "$BASE/printer.original" || native unknown-startup-backup "$@"
        OLD_LINE='ai_camera &'
        if [ "$(sha "$BASE/printer.original")" = "$TRIAL_INIT_SHA" ];then OLD_LINE=$TRIAL_LINE;fi
        PREVIOUS=$(awk -v old="$OLD_LINE" -v new="$NEW_LINE" '{line=$0;sub(/^[ \t]*/,"",line);if(line==old){match($0,/^[ \t]*/);print substr($0,1,RLENGTH) new;n++;next}print} END{if(n!=1)exit 1}' "$BASE/printer.original" | sha256sum | awk '{print $1}')
        [ "$CURRENT" = "$PREVIOUS" ] || native unknown-previous-startup "$@"
    elif [ "$(sha "$BASE/printer.original")" != "$STOCK_INIT_SHA" ];then native changed-firmware-backup "$@"
    fi
else
    CURRENT=''
fi
if [ "$CURRENT" != "$(cat "$SOURCE/patched.sha256")" ];then
    STAGE=/opt/usr/cc2-control/.camera-firmware-$$
    mkdir "$STAGE" || native cannot-stage "$@"
    for file in common.sh manage.sh boot-camera.sh SHA256SUMS libcc2-camera-reap-r2.so libcc2-camera-recovery-r2.so cc2-camera-reap-check printer.original patched.sha256;do
        cp "$SOURCE/$file" "$STAGE/$file" || native copy-failed "$@"
    done
    chmod 700 "$STAGE" "$STAGE/boot-camera.sh" "$STAGE/manage.sh" "$STAGE/cc2-camera-reap-check"
    [ "$DISABLED" -eq 1 ] || printf 'CAMERA_RECOVERY_R2_ENABLED\n' > "$STAGE/enabled"
    (cd "$STAGE" && sha256sum -c SHA256SUMS >/dev/null 2>&1) || native staged-checksums "$@"
    if [ -d "$BASE" ];then
        BACKUP=/opt/usr/cc2-control/camera-before-firmware-$$
        [ ! -e "$BACKUP" ] && mv "$BASE" "$BACKUP" || native cannot-retain-backup "$@"
    else BACKUP='';fi
    if ! mv "$STAGE" "$BASE";then
        [ -z "$BACKUP" ] || mv "$BACKUP" "$BASE" || true
        native commit-failed "$@"
    fi
    STAGE=''
fi
cleanup
trap - 0 1 2 15
printf 'mode=delegated component=persistent-r2 pid=%s\n' "$$" > /tmp/cc2-camera-firmware-status.txt
exec "$BASE/boot-camera.sh" "$@"
