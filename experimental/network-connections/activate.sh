#!/bin/sh
# Reuse a qualified mapped network shim; restart native services only if needed.
# The independent configuration watchdog must already be running for start.
set -eu
umask 077
PATH=/usr/sbin:/usr/bin:/sbin:/bin:/opt/bin
export PATH
ROOT=/opt/usr/cc2-network-manager
STATE=/opt/usr/cc2-network-state
LIB=$ROOT/libcc2-network-compat-r1.so
OLD=/tmp/cc2-network-compat-r1/libcc2-network-compat-r1.so
REAP=/opt/usr/cc2-control/camera/libcc2-camera-reap-r2.so
RECOVERY=/opt/usr/cc2-control/camera/libcc2-camera-recovery-r2.so
REACTOR=/opt/usr/cc2-reactor-runtime-v2/libcc2-reactor-v2.so
fail(){ echo "activation:$*";exit 1; }
one_pid(){ value=$(pidof "$1" 2>/dev/null || true);case "$value" in ''|*' '*|*[!0-9]*) return 1;;esac;printf '%s\n' "$value"; }
env_field(){ tr '\0' '\n' < "/proc/$1/environ" | awk -v key="$2" 'index($0,key "=")==1 {count++;value=substr($0,length(key)+2)} END {if(count>1)exit 1;print value}'; }
mapped(){
    pid=$1;role=$2
    grep -F 'libcc2-network-compat-r1.so' "/proc/$pid/maps" >/dev/null 2>&1 || return 1
    grep -F "pid=$pid role=$role patched=1" "/tmp/cc2-network-compat-r1.$role.status" >/dev/null 2>&1 || return 1
}
ready(){
    camera=$(one_pid ai_camera) && printer=$(one_pid elegoo_printer) || return 1
    mapped "$camera" camera && mapped "$printer" printer
}
qualify(){
    camera=$(one_pid ai_camera) || fail camera_process_count
    printer=$(one_pid elegoo_printer) || fail printer_process_count
    [ "$(readlink "/proc/$camera/exe")" = /opt/bin/ai_camera ] || fail camera_path
    [ "$(readlink "/proc/$printer/exe")" = /opt/usr/cc2-reactor-runtime-v2/elegoo_printer ] || fail printer_path
    [ "$(sha256sum "/proc/$camera/exe" | awk '{print $1}')" = ec5633b17de329d5b18c3f8d8de6ebf389391a04193536d976ba167022d07b99 ] || fail running_camera_profile
    [ "$(sha256sum "/proc/$printer/exe" | awk '{print $1}')" = 19712b5943b10bb8cb6aab9a4ed498ee962b28538b23c91b7ed003eb9e590745 ] || fail running_printer_profile
    for p in "$camera" "$printer";do [ "$(readlink "/proc/$p/cwd")" = / ] || fail working_directory;done
    expected=$(printf '%s\n' /opt/usr/cc2-reactor-runtime-v2/elegoo_printer /opt/inst/printer_dsp.cfg -s /opt/usr/cfg/autosave.cfg -a /tmp/elegoo_uds)
    [ "$(tr '\0' '\n' < "/proc/$printer/cmdline")" = "$expected" ] || fail printer_arguments
    c=$(env_field "$camera" LD_PRELOAD);p=$(env_field "$printer" LD_PRELOAD)
    case "$c" in "$REAP:$RECOVERY"|"$OLD:$REAP:$RECOVERY"|"$LIB:$REAP:$RECOVERY") ;;*) fail camera_preload;;esac
    case "$p" in "$REACTOR"|"$OLD:$REACTOR"|"$LIB:$REACTOR") ;;*) fail printer_preload;;esac
    for flag in CC2_CAMERA_REAP_TRIAL CC2_CAMERA_REAP_ENABLE_LAN CC2_CAMERA_RECOVERY_TRIAL;do [ "$(env_field "$camera" "$flag")" = 1 ] || fail camera_flags;done
    [ "$(env_field "$printer" CC2_REACTOR_RUNTIME_V2)" = 1 ] || fail reactor_flag
    for item in "$REAP" "$RECOVERY";do grep -F "$item" "/proc/$camera/maps" >/dev/null || fail camera_mapping;done
    grep -F "$REACTOR" "/proc/$printer/maps" >/dev/null || fail reactor_mapping
    for lib in "$LIB" "$OLD";do
        if grep -F "$lib" "/proc/$camera/maps" "/proc/$printer/maps" >/dev/null 2>&1;then
            [ "$(sha256sum "$lib" | awk '{print $1}')" = bde512195dd6bf3846f3103b5e1acbd89a62e5ee819e405389c97f5fc188ff3a ] || fail mapped_network_library
        fi
    done
}
stop_one(){
    p=$1;kill -TERM "$p" || return 1;n=0
    while kill -0 "$p" 2>/dev/null;do [ "$n" -lt 15 ] || return 1;sleep 1;n=$((n+1));done
}
launch_camera(){
    "$ROOT/control" --native-spawn camera "$TOKEN" "$1"
}
launch_printer(){
    "$ROOT/control" --native-spawn printer "$TOKEN" "$1"
}
case "${1:-}" in
 ready) qualify;ready;exit $?;;
 start|repair) ;;
 *) fail unknown_action;;
esac
TOKEN=${2:-}
case "$TOKEN" in ''|*[!0-9a-f]*) fail invalid_transaction;;esac
[ "${#TOKEN}" = 32 ] || fail invalid_transaction
SNAP="$STATE/activation-$TOKEN"
sh "$ROOT/profile.sh" ethernet || fail platform_profile
if [ "$1" = start ];then
    "$ROOT/control" --guard "${3:-8081}" || fail printer_not_ready
    qualify
    if ready;then echo 'activation:already_ready';exit 0;fi
    "$ROOT/control" --watch-ready "$TOKEN" || fail watchdog_not_ready
    [ ! -e "$SNAP" ] && [ ! -L "$SNAP" ] || fail snapshot_exists
    mkdir "$SNAP"
    for role in camera printer;do
        if [ "$role" = camera ];then pid=$camera;else pid=$printer;fi
        env_field "$pid" PATH > "$SNAP/$role.path"
        env_field "$pid" LD_LIBRARY_PATH > "$SNAP/$role.library"
        if mapped "$pid" "$role";then echo 1 > "$SNAP/$role.network";else echo 0 > "$SNAP/$role.network";fi
        [ -s "$SNAP/$role.path" ] || fail service_path_unavailable
    done
    stop_one "$camera" && stop_one "$printer" || fail service_stop_incomplete
    if pidof ai_camera elegoo_printer >/dev/null 2>&1;then fail another_native_process;fi
    launch_printer 1;launch_camera 1
    n=0;while ! ready && [ "$n" -lt 20 ];do sleep 1;n=$((n+1));done
    ready || fail runtime_not_confirmed
    echo ready > "$SNAP/activation.complete"
    echo 'activation:ready'
else
    [ -d "$SNAP" ] && [ ! -L "$SNAP" ] || exit 0
    [ ! -f "$SNAP/activation.complete" ] || exit 0
    if ! pidof elegoo_printer >/dev/null 2>&1;then launch_printer "$(cat "$SNAP/printer.network")";fi
    if ! pidof ai_camera >/dev/null 2>&1;then launch_camera "$(cat "$SNAP/camera.network")";fi
    one_pid ai_camera >/dev/null && one_pid elegoo_printer >/dev/null || fail repair_incomplete
    echo 'activation:repaired'
fi
