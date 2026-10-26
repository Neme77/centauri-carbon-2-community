#!/bin/sh
VENDOR_EXPECTED=c21126e78a63ac9140e7347c56f363d5e513495c58c06fc17e8ef97846f3c0a3
BASE=/opt/usr/cc2-reactor-runtime-v2
TARGET=/opt/usr/cc2-control
SERVICE=/etc/init.d/printer
INIT=/etc/init.d/cc2-control
hash() { sha256sum "$1" | awk '{print $1}'; }
idle_cold() {
    STATUS=$(wget -qO- http://127.0.0.1:8081/api/printer) || return 1
    UDS=$(wget -qO- http://127.0.0.1:8081/api/uds) || return 1
    case "$STATUS" in *'"connected":true'*'"machine":{"status":1,'*) ;; *) return 1;; esac
    AGE=$(printf '%s' "$STATUS" | sed -n 's/.*"last_message_age":\([0-9][0-9]*\)[,}].*/\1/p')
    case "$AGE" in ''|*[!0-9]*) return 1;; esac
    [ "$AGE" -le 15 ] || return 1
    case "$UDS" in *'"connected":true'*'"fresh":true'*) ;; *) return 1;; esac
    value() { printf '%s\n' "$UDS" | sed -n "s/.*\"$1\":\([^,}]*\).*/\1/p"; }
    awk -v n="$(value nozzle_target)" -v b="$(value bed_target)" -v v="$(value live_velocity)" \
        -v nt="$(value nozzle_temperature)" -v bt="$(value bed_temperature)" 'BEGIN {
        valid="^[0-9]+([.][0-9]+)?$";
        exit !(n~valid&&b~valid&&v~valid&&nt~valid&&bt~valid&&n+0==0&&b+0==0&&v+0==0&&nt+0<=50&&bt+0<=45)
    }'
}
one_printer() {
    PID=$(pidof elegoo_printer || true)
    set -- $PID
    [ "$#" = 1 ] && [ "$(hash "/proc/$PID/exe")" = "$VENDOR_EXPECTED" ]
}
