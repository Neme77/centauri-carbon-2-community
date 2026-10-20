#!/bin/sh
# Read diagnostics only; create one private /tmp text report. Never restart.
set -eu
umask 077
TESTER=${1:-T1}
PHASE=${2:-status}
for VALUE in "$TESTER" "$PHASE";do
    case "$VALUE" in ''|*[!A-Za-z0-9_-]*) echo 'Use only letters, digits, underscore or dash for tester/phase.' >&2;exit 2;;esac
    [ "${#VALUE}" -le 32 ] || exit 2
done
STAMP=$(date -u +%Y%m%d-%H%M%S)
REPORT=/tmp/cc2-camera-report-$TESTER-$PHASE-$STAMP-$$.txt
(set -C; : > "$REPORT") || exit 1
PIDS=$(pidof ai_camera 2>/dev/null || true)
marker(){
    FILE=$1
    if [ ! -r "$FILE" ];then echo 'marker=missing';return;fi
    awk -v pid="$PIDS" '
    {
        matched=0;out=""
        for(i=1;i<=NF;i++)if($i=="pid="pid)matched=1
        if(!matched)next
        for(i=1;i<=NF;i++){
            if(split($i,a,"=")!=2)continue
            key=a[1];value=a[2];valid=0
            if(key=="trial"&&(value=="R2"||value=="RECOVERY_R2"))valid=1
            if(key~/^(pid|patched|enable_lan_once|spawned|reaped|rejected|retained|last_error|ready|epoch|announced|suppressed|failed|initial_calls|initial_returns|connections)$/&&value~/^[0-9]+$/)valid=1
            if(key=="executable_sha256"&&length(value)==64&&value!~/[^0-9a-f]/)valid=1
            if(valid)out=out (out==""?"":" ") $i
        }
        if(out!="")print out
        found=1
    }
    END {if(!found)print "marker=missing_or_stale"}' "$FILE"
}
{
    echo 'CC2 CAMERA RECOVERY R2 — MANUAL TEST REPORT'
    echo "tester=$TESTER phase=$PHASE device_time_utc=$STAMP"
    echo 'This report does not prove actual video or AI behavior.'
    echo "printer_pids=$(pidof elegoo_printer 2>/dev/null || true)"
    case "$PIDS" in
        ''|*' '*|*[!0-9]*) echo 'camera=missing_or_multiple';;
        *)
            echo "camera_pid=$PIDS"
            if [ -r "/proc/$PIDS/status" ];then grep -E '^(State:|VmSize:|VmRSS:|Threads:)' "/proc/$PIDS/status" || true;fi
            if [ -r "/proc/$PIDS/exe" ];then
                DIGEST=$(sha256sum "/proc/$PIDS/exe" 2>/dev/null | awk '{print $1}') || DIGEST=unavailable
                echo "camera_executable_sha256=$DIGEST"
            fi
            for LIB in libcc2-camera-reap-r2.so libcc2-camera-recovery-r2.so;do
                if grep -F "$LIB" "/proc/$PIDS/maps" >/dev/null 2>&1;then echo "$LIB=mapped_by_name";else echo "$LIB=not_mapped";fi
            done
            marker /tmp/cc2-camera-reap.status
            marker /tmp/cc2-camera-recovery.status
            ;;
    esac
    grep -E '^(MemTotal:|MemAvailable:|MemFree:)' /proc/meminfo || true
    awk '$NF=="/tmp/aicamera_uds"{entries++;if($6=="03")connected++;if($4=="00010000"&&$6=="01")listening++}
         END {printf "control_socket_entries=%d listening=%d connected=%d\n",entries+0,listening+0,connected+0}' /proc/net/unix
    echo 'No HTTP requests, camera payload logs, process signals or startup changes were made.'
} >> "$REPORT"
cat "$REPORT"
echo "Report saved: $REPORT"
