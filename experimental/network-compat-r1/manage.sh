#!/bin/sh
# Supervised temporary trial. No startup, firmware, DNS or firewall writes.
set -u
umask 077
PATH=/usr/sbin:/usr/bin:/sbin:/bin
export PATH
BASE=/tmp/cc2-network-compat-r1
MODE=/tmp/cc2-network-compat-r1.mode
LOG=/opt/usr/cc2-network-compat-r1-logs
CAMERA=/opt/bin/ai_camera
PRINTER=/opt/usr/cc2-reactor-runtime-v2/elegoo_printer
REACTOR=/opt/usr/cc2-reactor-runtime-v2/libcc2-reactor-v2.so
REAP=/opt/usr/cc2-control/camera/libcc2-camera-reap-r2.so
RECOVERY=/opt/usr/cc2-control/camera/libcc2-camera-recovery-r2.so
LIB=$BASE/libcc2-network-compat-r1.so
CAMERA_PATH=/usr/sbin:/usr/bin:/sbin:/bin:/opt/bin
PRINTER_PATH=$CAMERA_PATH
CAMERA_LIBRARY_PATH=/opt/lib
PRINTER_LIBRARY_PATH=/opt/lib
fail(){ echo "STOP: $*" >&2; exit 1; }
sha(){ sha256sum "$1" | awk '{print $1}'; }
clock_now(){ awk '{print int($1)}' /proc/uptime; }
one_pid(){
    value=$(pidof "$1" 2>/dev/null || true)
    case "$value" in ''|*' '*|*[!0-9]*) return 1;; esac
    printf '%s\n' "$value"
}
set_mode(){
    [ -d "$STATE" ] && [ ! -L "$STATE" ] || return 1
    [ ! -d "$MODE" ] || return 1
    printf '%s\n' "$1" > "$STATE/mode.next" && chmod 600 "$STATE/mode.next" && mv -f "$STATE/mode.next" "$MODE"
}
current_state(){
    [ -f "$BASE/active" ] && [ ! -L "$BASE/active" ] || return 1
    STATE=$(cat "$BASE/active")
    case "$STATE" in "$BASE"/state.*) ;; *) return 1;; esac
    suffix=${STATE#"$BASE"/state.}
    case "$suffix" in ''|*[!0-9.]*) return 1;; esac
    [ -d "$STATE" ] && [ ! -L "$STATE" ]
}
library_loaded(){ grep -F "$LIB" "/proc/$1/maps" >/dev/null 2>&1; }
env_field(){
    tr '\0' '\n' < "/proc/$1/environ" | awk -v key="$2" '
    index($0,key "=")==1 {count++;value=substr($0,length(key)+2)}
    END {if(count>1)exit 1;print value}'
}
runtime_ready(){
    for role in camera printer; do
        if [ "$role" = camera ];then name=ai_camera;else name=elegoo_printer;fi
        pid=$(one_pid "$name") || return 1
        library_loaded "$pid" || return 1
        grep -F "pid=$pid role=$role patched=1" "/tmp/cc2-network-compat-r1.$role.status" >/dev/null 2>&1 || return 1
    done
}
qualified_processes(){
    camera_pid=$(one_pid ai_camera) || fail 'Exactly one camera required'
    printer_pid=$(one_pid elegoo_printer) || fail 'Exactly one printer program required'
    [ "$(readlink "/proc/$camera_pid/exe")" = "$CAMERA" ] || fail 'Camera path changed'
    [ "$(readlink "/proc/$printer_pid/exe")" = "$PRINTER" ] || fail 'Printer runtime path changed'
    [ "$(sha "/proc/$camera_pid/exe")" = ec5633b17de329d5b18c3f8d8de6ebf389391a04193536d976ba167022d07b99 ] || fail 'Running camera changed'
    [ "$(sha "/proc/$printer_pid/exe")" = 19712b5943b10bb8cb6aab9a4ed498ee962b28538b23c91b7ed003eb9e590745 ] || fail 'Running printer changed'
    for pid in "$camera_pid" "$printer_pid";do [ "$(readlink "/proc/$pid/cwd")" = / ] || fail 'Working directory changed';done
    expected=$(printf '%s\n' "$PRINTER" /opt/inst/printer_dsp.cfg -s /opt/usr/cfg/autosave.cfg -a /tmp/elegoo_uds)
    actual=$(tr '\0' '\n' < "/proc/$printer_pid/cmdline")
    [ "$actual" = "$expected" ] || fail 'Printer arguments changed'
    grep -F "$REAP" "/proc/$camera_pid/maps" >/dev/null || fail 'Camera reap R2 not loaded'
    grep -F "$RECOVERY" "/proc/$camera_pid/maps" >/dev/null || fail 'Camera recovery R2 not loaded'
    grep -F "$REACTOR" "/proc/$printer_pid/maps" >/dev/null || fail 'Reactor not loaded'
    camera_preload=$(env_field "$camera_pid" LD_PRELOAD) || fail 'Cannot read camera loader environment'
    printer_preload=$(env_field "$printer_pid" LD_PRELOAD) || fail 'Cannot read printer loader environment'
    case "$camera_preload" in "$REAP:$RECOVERY"|"$LIB:$REAP:$RECOVERY") ;; *) fail 'Unreviewed camera preload; refusing to discard libraries';; esac
    case "$printer_preload" in "$REACTOR"|"$LIB:$REACTOR") ;; *) fail 'Unreviewed printer preload; refusing to discard libraries';; esac
    CAMERA_PATH=$(env_field "$camera_pid" PATH) || fail 'Cannot read camera PATH'
    PRINTER_PATH=$(env_field "$printer_pid" PATH) || fail 'Cannot read printer PATH'
    CAMERA_LIBRARY_PATH=$(env_field "$camera_pid" LD_LIBRARY_PATH) || fail 'Cannot read camera library path'
    PRINTER_LIBRARY_PATH=$(env_field "$printer_pid" LD_LIBRARY_PATH) || fail 'Cannot read printer library path'
    [ -n "$CAMERA_PATH" ] && [ -n "$PRINTER_PATH" ] || fail 'Original service PATH unavailable'
    [ "$(env_field "$printer_pid" CC2_REACTOR_RUNTIME_V2)" = 1 ] || fail 'Original reactor not enabled'
    for name in CC2_CAMERA_REAP_TRIAL CC2_CAMERA_REAP_ENABLE_LAN CC2_CAMERA_RECOVERY_TRIAL;do
        [ "$(env_field "$camera_pid" "$name")" = 1 ] || fail 'Original camera flags differ'
    done
}
check(){
    [ "$(id -u)" = 0 ] || fail 'Root required'
    [ -d "$BASE" ] && [ ! -L "$BASE" ] || fail 'Trial directory unavailable'
    (cd "$BASE" && sha256sum -c SHA256SUMS) || fail 'Payload checksum failed'
    sh "$BASE/thermal_selftest.sh" || fail 'Native thermal parser self-test failed'
    sh "$BASE/preflight.sh" || fail 'Printer/network preflight failed'
    if [ -f /tmp/cc2-lan-cloud-r1.state/watch.pid ];then
        previous_watch=$(cat /tmp/cc2-lan-cloud-r1.state/watch.pid)
        case "$previous_watch" in ''|*[!0-9]*) fail 'Previous cloud trial PID is invalid';; esac
        if kill -0 "$previous_watch" 2>/dev/null &&
           tr '\0' ' ' < "/proc/$previous_watch/cmdline" | grep -F '/tmp/cc2-lan-cloud-r1.sh' >/dev/null;then
            fail 'Previous cloud trial watchdog still active; wait for it to finish'
        fi
    fi
    [ "$(sha "$REAP")" = 8ef3a5c215462e3c9f32c3d4b85ea23e27c41e330d53b22d7cdeca9d100d016a ] || fail 'Camera reap library differs'
    [ "$(sha "$RECOVERY")" = 08f2455a99fa5294153ae64ce34117c02f4f8d2dff8527dfd8386aae361a289a ] || fail 'Camera recovery library differs'
    [ "$(sha "$REACTOR")" = 4639364c68489ec40c4e0c4f425550f3aa19e44c2ee61ed641affdc560e7e92c ] || fail 'Reactor profile differs from this V4.4.2 trial'
    qualified_processes
    helper=/opt/usr/cc2-control/camera/cc2-camera-reap-check
    [ "$(sha "$helper")" = dce1180e3c04512fea1c4f9868b131f3fbe9bda187ebc9342f9db633f0a57ea0 ] || fail 'Runtime helper differs'
    CC2_NETWORK_COMPAT_TRIAL=0 CC2_CAMERA_REAP_TRIAL=0 CC2_CAMERA_RECOVERY_TRIAL=0 CC2_REACTOR_RUNTIME_V2=0 LD_LIBRARY_PATH=/opt/lib LD_PRELOAD="$LIB:$REAP:$RECOVERY:$REACTOR" "$helper" || fail 'Runtime loader check failed'
    echo 'CHECK PASS: payload, native processes, existing camera/reactor patches and inert loader. No service restarted.'
}
launch_camera(){
    compatibility=$1
    preload="$REAP:$RECOVERY"
    [ "$compatibility" = 0 ] || preload="$LIB:$preload"
    (trap '' HUP;ulimit -c 0;cd / || exit 1
     PATH="$CAMERA_PATH" CC2_NETWORK_COMPAT_TRIAL="$compatibility" CC2_CAMERA_REAP_TRIAL=1 CC2_CAMERA_REAP_ENABLE_LAN=1 CC2_CAMERA_RECOVERY_TRIAL=1 LD_LIBRARY_PATH="$CAMERA_LIBRARY_PATH" LD_PRELOAD="$preload" exec "$CAMERA") </dev/null >> "$LOG/camera.log" 2>&1 &
}
launch_printer(){
    compatibility=$1
    preload="$REACTOR"
    [ "$compatibility" = 0 ] || preload="$LIB:$preload"
    (trap '' HUP;ulimit -c 0;cd / || exit 1
     PATH="$PRINTER_PATH" CC2_NETWORK_COMPAT_TRIAL="$compatibility" CC2_REACTOR_RUNTIME_V2=1 LD_LIBRARY_PATH="$PRINTER_LIBRARY_PATH" LD_PRELOAD="$preload" exec "$PRINTER" /opt/inst/printer_dsp.cfg -s /opt/usr/cfg/autosave.cfg -a /tmp/elegoo_uds) </dev/null >> "$LOG/printer.log" 2>&1 &
}
wifi_ready(){
    wpa_cli -p /opt/usr/wifi/sockets -i wlan0 status | grep -q '^wpa_state=COMPLETED$' &&
    ip -4 addr show dev wlan0 | grep -q 'inet 192.168.1.103/24 ' &&
    ip -4 route show table main | grep -q '^default via 192.168.1.1 dev wlan0'
}
restore(){
    [ -d "$STATE" ] || return 1
    [ ! -f "$STATE/restored" ] || return 0
    mkdir "$STATE/restoring" 2>/dev/null || return 1
    for field in camera-path printer-path camera-library-path printer-library-path;do
        if [ -f "$STATE/$field" ];then
            case "$field" in
                camera-path) CAMERA_PATH=$(cat "$STATE/$field");;
                printer-path) PRINTER_PATH=$(cat "$STATE/$field");;
                camera-library-path) CAMERA_LIBRARY_PATH=$(cat "$STATE/$field");;
                printer-library-path) PRINTER_LIBRARY_PATH=$(cat "$STATE/$field");;
            esac
        fi
    done
    ok=1
    if ! wifi_ready;then
        ip link set dev wlan0 up || ok=0
        wpa_cli -p /opt/usr/wifi/sockets -i wlan0 reconnect > "$STATE/reconnect.log" 2>&1 || ok=0
        count=0
        while ! wifi_ready && [ "$count" -lt 20 ];do sleep 1;count=$((count+1));done
    fi
    if ! wifi_ready;then
        echo 'RESTORE INCOMPLETE: Wi-Fi not ready; wired routing preserved. Use the Ethernet address.' > "$STATE/error"
        rmdir "$STATE/restoring";cat "$STATE/error";return 1
    fi
    # Missing mode file restores the original native methods via trampolines.
    rm -f "$MODE" || ok=0
    if [ -f "$STATE/rule-intent" ] && ip -4 rule show | grep -q '^22040:.*from all lookup 200';then
        ip -4 rule del priority 22040 from all table 200 || ok=0
    fi
    # A launch interrupted between stop/start must not leave a service missing.
    if ! pidof elegoo_printer >/dev/null 2>&1;then launch_printer 0;sleep 2;fi
    if ! pidof ai_camera >/dev/null 2>&1;then launch_camera 0;sleep 2;fi
    one_pid elegoo_printer >/dev/null && one_pid ai_camera >/dev/null || ok=0
    if [ "$ok" = 1 ];then
        echo 'RESTORED: Wi-Fi connected, native interface selection restored, trial route removed. Verify local/Matrix video.' > "$STATE/restored"
        cat "$STATE/restored"
    else
        echo 'RESTORE INCOMPLETE: inspect logs; no automatic reboot.' > "$STATE/error";cat "$STATE/error"
    fi
    rmdir "$STATE/restoring" 2>/dev/null || true
    [ "$ok" = 1 ]
}
arm(){
    if current_state && [ ! -f "$STATE/restored" ];then
        fail 'A trial is still active or un-restored; use status or restore'
    fi
    STATE="$BASE/state.$(date +%s).$$"
    mkdir "$STATE" || fail 'Cannot create private trial state'
    printf '%s\n' "$CAMERA_PATH" > "$STATE/camera-path"
    printf '%s\n' "$PRINTER_PATH" > "$STATE/printer-path"
    printf '%s\n' "$CAMERA_LIBRARY_PATH" > "$STATE/camera-library-path"
    printf '%s\n' "$PRINTER_LIBRARY_PATH" > "$STATE/printer-library-path"
    [ ! -L "$LOG" ] || fail 'Unexpected log directory symlink'
    mkdir -p "$LOG" && chmod 700 "$LOG" || fail 'Cannot create disk log directory'
    printf '%s\n' "$STATE" > "$BASE/active.next" && mv -f "$BASE/active.next" "$BASE/active" || fail 'Cannot publish trial state'
    deadline=$(( $(clock_now)+600 ))
    echo "$deadline" > "$STATE/deadline"
    (trap '' HUP;exec sh "$BASE/manage.sh" __watch "$STATE") </dev/null > "$STATE/watch.log" 2>&1 &
    watch_pid=$!;echo "$watch_pid" > "$STATE/watch.pid"
    sleep 1
    kill -0 "$watch_pid" 2>/dev/null && [ -f "$STATE/ready" ] || fail 'Independent watchdog unavailable; no service restarted'
}
stop_one(){
    pid=$1
    kill -TERM "$pid" || return 1
    count=0
    while kill -0 "$pid" 2>/dev/null;do
        [ "$count" -lt 15 ] || return 1
        sleep 1;count=$((count+1))
    done
}
status(){
    printf 'Mode file: ';if [ -f "$MODE" ];then cat "$MODE";else echo 'native';fi
    for role in camera printer;do
        if [ "$role" = camera ];then name=ai_camera;else name=elegoo_printer;fi
        pid=$(one_pid "$name") || { echo "$role process missing/ambiguous";continue; }
        echo "$role PID: $pid"
        grep -E '^(VmSize:|VmRSS:|Threads:)' "/proc/$pid/status"
        if library_loaded "$pid";then echo 'Network compatibility library: mapped';fi
        if [ -f "/tmp/cc2-network-compat-r1.$role.status" ];then cat "/tmp/cc2-network-compat-r1.$role.status";fi
    done
    ip -4 route get 1.1.1.1
    printf 'Wi-Fi operstate: ';cat /sys/class/net/wlan0/operstate
    if current_state;then
        for file in restored error;do if [ -f "$STATE/$file" ];then cat "$STATE/$file";fi;done
        if [ -f "$STATE/deadline" ];then echo "Watchdog remaining seconds: $(( $(cat "$STATE/deadline")-$(clock_now) ))";fi
    fi
    echo 'Runtime markers do not prove local video, Matrix, RTC, AI or printing.'
}
case "${1:-check}" in
    check) check;;
    status) status;;
    restore) current_state || fail 'No trial state';restore;;
    __watch)
        STATE=${2:-}
        case "$STATE" in "$BASE"/state.*) ;; *) fail 'Invalid watchdog state';; esac
        [ -d "$STATE" ] && [ ! -L "$STATE" ] || fail 'Missing watchdog state'
        echo ready > "$STATE/ready"
        while [ ! -f "$STATE/restored" ];do
            deadline=$(cat "$STATE/deadline") || break
            case "$deadline" in ''|*[!0-9]*) break;; esac
            [ "$(clock_now)" -lt "$deadline" ] || break
            sleep 1
        done
        if [ ! -f "$STATE/restored" ];then restore;fi
        ;;
    start)
        check
        if library_loaded "$camera_pid" || library_loaded "$printer_pid";then fail 'Module already loaded; use ethernet or restore, not another restart';fi
        arm
        set_mode wifi || { restore;fail 'Cannot set baseline mode'; }
        old_camera=$camera_pid;old_printer=$printer_pid
        echo "Stopping qualified camera $old_camera and printer $old_printer; watchdog armed."
        stop_one "$old_camera" && stop_one "$old_printer" || { restore;fail 'Service did not terminate cleanly; no forced kill'; }
        if pidof ai_camera >/dev/null 2>&1 || pidof elegoo_printer >/dev/null 2>&1;then restore;fail 'Another service appeared';fi
        launch_printer 1;launch_camera 1
        count=0
        while ! runtime_ready && [ "$count" -lt 20 ];do sleep 1;count=$((count+1));done
        runtime_ready || { restore;fail 'Activation not confirmed; inspect logs'; }
        sleep 3
        runtime_ready || { restore;fail 'Trial service exited'; }
        status
        echo 'WIFI BASELINE ACTIVE. Verify ordinary local and Matrix video before using ethernet mode.'
        echo 'Baseline automatically restores native selection after 10 minutes. No persistent startup changes.'
        ;;
    ethernet)
        check;runtime_ready || fail 'Start and validate Wi-Fi baseline first'
        if ! current_state || [ -f "$STATE/restored" ];then arm;fi
        watch_pid=$(cat "$STATE/watch.pid")
        case "$watch_pid" in ''|*[!0-9]*) fail 'Invalid watchdog PID';; esac
        kill -0 "$watch_pid" 2>/dev/null || fail 'Watchdog absent; restore before retrying'
        echo $(( $(clock_now)+180 )) > "$STATE/deadline.next" && mv -f "$STATE/deadline.next" "$STATE/deadline" || fail 'Cannot arm Ethernet deadline'
        : > "$STATE/rule-intent"
        ip -4 rule add priority 22040 from all table 200 || { restore;fail 'Wired rule failed'; }
        set_mode ethernet || { restore;fail 'Cannot select Ethernet for native services'; }
        ip link set dev wlan0 down || { restore;fail 'Cannot lower Wi-Fi'; }
        sleep 2
        flags=$(cat /sys/class/net/wlan0/flags) || { restore;fail 'Cannot read Wi-Fi flags'; }
        if [ $((flags&1)) -ne 0 ];then restore;fail 'Native manager raised Wi-Fi; test inconclusive';fi
        status
        echo 'ETHERNET TRIAL ACTIVE. Test local video and Matrix now; automatic Wi-Fi restore after 3 minutes.'
        ;;
    *) fail 'Use check, start, ethernet, status or restore';;
esac
