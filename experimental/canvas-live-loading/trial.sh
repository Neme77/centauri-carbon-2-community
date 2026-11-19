#!/bin/sh
# Temporary native-only trial for the qualified 02.01.01.07 RFID stack.
set -eu
PATH=/opt/bin:/usr/sbin:/usr/bin:/sbin:/bin
export PATH
umask 077

fail() { printf 'STOP: %s\n' "$*" >&2; exit 1; }
base=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd -P)
[ "$(id -u)" -eq 0 ] && [ "$base" = /tmp/cc2-canvas-live-loading ] &&
    ls -ldn "$base" | awk '$1=="drwx------" && $3==0 {ok=1} END {exit !ok}' ||
    fail 'use a private root-owned /tmp/cc2-canvas-live-loading directory'
cd /

app=/opt/usr/cc2-control/cc2-control
native=/opt/usr/cc2-reactor-runtime-v2/elegoo_printer
guarded=/opt/usr/cc2-rfid/guarded-entry.sh
lib=$base/libcc2-canvas-live-loading.so
entry=$base/native-entry.sh
lib_sha=b5b37346335dfb8b403f3465cdaecfc705769461322a1387d92793019f34af10
original_preloads=/opt/usr/cc2-reactor-runtime-v2/libcc2-reactor-v2.so:/opt/usr/cc2-cavity-floor/libcc2-cavity-floor-r1.so:/opt/usr/cc2-rfid/libcc2-rfid.so
lock=/tmp/cc2-control-install.lock
lock_inode=
recover=0

web() { /bin/wget -q -T 4 -O - "http://127.0.0.1:8081/api/$1"; }
web_idle() {
    response=$(web printer) || return 1
    case "$response" in *'"connected":true'*'"machine":{"status":1,'*) ;; *) return 1;; esac
    age=$(printf '%s\n' "$response" | sed -n 's/.*"last_message_age":\([0-9][0-9]*\),.*/\1/p')
    case "$age" in ''|*[!0-9]*) return 1;; esac
    [ "$age" -le 15 ] || return 1
    queued=$(web queue) || return 1
    case "$queued" in *'"active":null'*) return 0;; *) return 1;; esac
}

native_idle() { "$app" --installer-native-idle; }
native_pid() {
    pid=$(pidof elegoo_printer) || return 1
    case "$pid" in ''|*[!0-9]*) return 1;; esac
    [ "$(readlink "/proc/$pid/exe")" = "$native" ] || return 1
    args=$(tr '\000' '\n' <"/proc/$pid/cmdline")
    [ "$args" = "$(printf '%s\n' "$native" /opt/inst/printer_dsp.cfg -s /opt/usr/cfg/autosave.cfg -a /tmp/elegoo_uds)" ] ||
        return 1
    printf '%s\n' "$pid"
}

native_mode() {
    preloads=$(tr '\000' '\n' <"/proc/$1/environ" | sed -n 's/^LD_PRELOAD=//p')
    # Read just the first instruction at the qualified prefeed branch.
    word=$(dd if="/proc/$1/mem" bs=1 skip=$((0x005e023c)) count=4 2>/dev/null |
        hexdump -v -e '4/1 "%02x"')
    if [ "$preloads" = "$original_preloads" ] && [ "$word" = 58301be5 ]; then
        printf '%s\n' stock
    elif [ "$preloads" = "$original_preloads:$lib" ] && [ "$word" = 04f01fe5 ] &&
        grep -Fq " $lib" "/proc/$1/maps"; then
        printf '%s\n' trial
    else
        return 1
    fi
}

profile() {
    sha256sum -c - <<'HASHES'
19712b5943b10bb8cb6aab9a4ed498ee962b28538b23c91b7ed003eb9e590745  /opt/usr/cc2-reactor-runtime-v2/elegoo_printer
05e4614779fc59b43c604eda7d85f1e8534b24d3cf284b16226c9c3bbc975118  /opt/bin/run_printer.sh
b79c0344f5fe009fc860cf58e506e865c81810da17cf741400aba2058db0390e  /etc/init.d/cc2-reactor-launch
09168e95d4ac4cd40d0b39d15aace66f6fd99cdda25c1a683cf25bde40f95fb5  /opt/usr/cc2-rfid/startup-worker
fe12d3d3db1b46547f9ea151b5d88ea8bca6040750ae7e52b1b7323f0831ff0d  /opt/usr/cc2-rfid/recover-worker
3e1199c95b00d7e9a65b2a397044de6ac0c65b0140e82275d94cd9408819683d  /opt/usr/cc2-rfid/guarded.sha256
923d5de851cff6f1a4c8d658dbc0e864905781dc213f2caf3a7de8934dcfdee3  /opt/usr/cc2-rfid/guarded-entry.sh
4639364c68489ec40c4e0c4f425550f3aa19e44c2ee61ed641affdc560e7e92c  /opt/usr/cc2-reactor-runtime-v2/libcc2-reactor-v2.so
52d400605dd51f89eb95248c42a1661b3c0e60fadec5453d27a987729332fb10  /opt/usr/cc2-cavity-floor/libcc2-cavity-floor-r1.so
fe6210eebfde592e4b0d276d2097abb0cfa3d53264da7ee9d83d8f24d7597ce2  /opt/usr/cc2-rfid/libcc2-rfid.so
082872ea999fec46185011234bfa369df9744809ffb5f98285b5e7ddcb282b1f  /opt/usr/cc2-control/cc2-control
HASHES
}

boot_guard() {
    rc=0
    policy=$(/opt/usr/cc2-rfid/recover-worker --boot-policy) || rc=$?
    [ "$rc" -eq 10 ] && [ "$policy" = BOOT_POLICY=guarded ] || return 1
    # Phase 5 is the active phase of the checked lifecycle record.
    [ "$(dd if=/opt/usr/cc2-rfid/lifecycle bs=1 skip=16 count=1 2>/dev/null |
        hexdump -v -e '1/1 "%u"')" = 5 ] || return 1
    /etc/init.d/cc2-reactor-launch --prepare-only
}

prepare_entry() {
    [ -f "$lib" ] && [ ! -L "$lib" ] || return 1
    printf '%s  %s\n' "$lib_sha" "$lib" | sha256sum -c - || return 1
    # Extend the exact guarded entry in tmpfs. The manifest and loader checks
    # run before exec, and the qualified stack uses the disabled network shim.
    temp=$(mktemp "$base/entry.XXXXXX") || return 1
    if awk -v lib="$lib" -v sha="$lib_sha" '
        $0=="network=0" { print "if \"$NETWORK_CONTROL\" --enabled; then stop; fi"; network++ }
        $0=="preloads=$CORE_PRELOADS" {
            print "printf \"%s  %s\\n\" " sha " " lib " | sha256sum -c - || stop"
            $0="preloads=$CORE_PRELOADS:" lib; preload++
        }
        $0=="export CC2_REACTOR_RUNTIME_V2=1" {
            print "if [ \"${1:-}\" = --check-loaders ]; then exit 0; fi"; checker++
        }
        { print }
        END { exit !(network==1 && preload==1 && checker==1) }
    ' "$guarded" >"$temp" && sh -n "$temp"; then
        mv "$temp" "$entry"
    else
        rm -f "$temp"
        return 1
    fi
}

wait_dead() {
    remaining=15
    while pidof elegoo_printer >/dev/null 2>&1 && [ "$remaining" -gt 0 ]; do
        sleep 1
        remaining=$((remaining - 1))
    done
    ! pidof elegoo_printer >/dev/null 2>&1
}

wait_native() {
    wanted=$1 remaining=30
    while [ "$remaining" -gt 0 ]; do
        pid=$(native_pid) || pid=
        if [ -n "$pid" ] && [ "$(native_mode "$pid" 2>/dev/null)" = "$wanted" ] &&
            native_idle >/dev/null 2>&1 && "$app" --installer-native-canvas; then
            return 0
        fi
        sleep 1
        remaining=$((remaining - 1))
    done
    return 1
}

uds_connections() {
    response=$(web uds) || return 1
    case "$response" in *'"fresh":true'*) ;; *) return 1;; esac
    connections=$(printf '%s\n' "$response" | sed -n 's/.*"connections":\([0-9][0-9]*\),.*/\1/p')
    case "$connections" in ''|*[!0-9]*) return 1;; esac
    printf '%s\n' "$connections"
}

wait_panel() {
    before=$1 remaining=45
    while [ "$remaining" -gt 0 ]; do
        after=$(uds_connections) || after=
        if [ -n "$after" ] && [ "$after" -gt "$before" ]; then
            health=$(web health) || health=
            case "$health" in *'"mqtt_registered":true'*'"snapshot_received":true'*)
                if web_idle; then return 0; fi;;
            esac
        fi
        sleep 1
        remaining=$((remaining - 1))
    done
    return 1
}

start_stock() {
    boot_guard || return 1
    sh /opt/bin/run_printer.sh start </dev/null >"$base/native-restore.log" 2>&1 ||
        return 1
    wait_native stock
}

restore_after_failure() {
    if pidof elegoo_printer >/dev/null 2>&1; then
        pid=$(native_pid) || return 1
        mode=$(native_mode "$pid") || return 1
        if [ "$mode" = stock ]; then wait_native stock; return $?; fi
        native_idle && sh /opt/bin/run_printer.sh stop && wait_dead || return 1
    fi
    start_stock
}

cleanup() {
    rc=$?
    trap - EXIT
    trap '' HUP INT TERM
    if [ "$recover" -eq 1 ]; then
        restore_after_failure || printf '%s\n' 'STOP: automatic rollback incomplete' >&2
    fi
    if [ -n "$lock_inode" ]; then
        actual_inode=$(ls -di "$lock" 2>/dev/null | awk '{print $1}')
        if [ "$actual_inode" != "$lock_inode" ] || ! rmdir "$lock"; then
            printf '%s\n' 'STOP: maintenance lock identity changed or release failed' >&2
            rc=1
        fi
    fi
    exit "$rc"
}

check_stock() {
    profile && boot_guard || fail 'unexpected native profile'
    pid=$(native_pid) || fail 'expected one native printer process'
    [ "$(native_mode "$pid")" = stock ] || fail 'expected the qualified stock startup'
    web_idle && native_idle || fail 'printer must be freshly idle and cold'
    prepare_entry && sh "$entry" --check-loaders || fail 'trial loader qualification'
}

transition() {
    wanted=$1
    mkdir "$lock" || fail 'another maintenance operation holds the lock'
    lock_inode=$(ls -di "$lock" | awk '{print $1}')
    [ -n "$lock_inode" ] || fail 'cannot identify the maintenance lock'
    trap cleanup EXIT
    trap 'exit 1' HUP INT TERM
    web_idle && native_idle || fail 'printer became busy or hot'
    [ "$(native_pid)" = "$original_pid" ] || fail 'native process changed since preflight'
    before=$(uds_connections) || fail 'CC2 Control has no fresh native telemetry'
    recover=1
    sh /opt/bin/run_printer.sh stop && wait_dead || fail 'native process did not stop'
    if [ "$wanted" = trial ]; then
        boot_guard || fail 'native boot policy changed'
        (
            trap '' HUP
            export LD_BIND_NOW=1
            exec /bin/sh "$entry" /opt/inst/printer_dsp.cfg -s /opt/usr/cfg/autosave.cfg -a /tmp/elegoo_uds
        ) </dev/null >"$base/native-trial.log" 2>&1 &
        wait_native trial || fail 'trial did not reach idle with CANVAS connected'
    else
        start_stock || fail 'stock startup did not reach idle with CANVAS connected'
    fi
    wait_panel "$before" || fail 'CC2 Control did not confirm a native reconnect'
    recover=0
    printf 'READY: %s native process, CANVAS connected, CC2 Control reconnected\n' "$wanted"
}

case "${1:-}" in
    check) check_stock; printf '%s\n' 'PASS: qualified idle printer and trial libraries';;
    start) check_stock; original_pid=$pid; transition trial;;
    stop)
        profile && boot_guard || fail 'unexpected native profile'
        original_pid=$(native_pid) || fail 'expected one native printer process'
        [ "$(native_mode "$original_pid")" = trial ] || fail 'expected the qualified trial process'
        transition stock
        ;;
    status)
        pid=$(native_pid) && mode=$(native_mode "$pid") || fail 'unknown native process or patch'
        printf 'Native PID %s, mode %s\n' "$pid" "$mode"
        ;;
    *) fail 'use check, start, stop, or status';;
esac
