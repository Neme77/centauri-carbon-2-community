#!/bin/sh
# One temporary trial for the already qualified UE300/routing configuration.
# No firmware, stored network, DNS, firewall or default main-route changes.
set -u
PATH=/usr/sbin:/usr/bin:/sbin:/bin
export PATH
umask 077
STATE=/tmp/cc2-lan-cloud-r1.state
SELF=/tmp/cc2-lan-cloud-r1.sh
BASE=$(CDPATH= cd "$(dirname "$0")" && pwd)
WPA=/opt/usr/wifi/sockets
fail() { echo "STOP: $*" >&2; exit 1; }
wpa() { wpa_cli -p "$WPA" -i wlan0 "$@"; }
wifi_up() { flags=$(cat /sys/class/net/wlan0/flags) || return 1; [ $((flags & 1)) -eq 1 ]; }
read_fresh_printer() {
    attempt=0
    while [ "$attempt" -lt 15 ]; do
        data=$(wget -qO- http://127.0.0.1:8081/api/printer) || fail 'Printer status unavailable'
        age=$(printf '%s\n' "$data" | awk -F'"last_message_age":' 'NF==2 {split($2,a,","); print a[1]}')
        case "$age" in ''|*[!0-9]*) fail 'Invalid telemetry age';; esac
        if [ "$age" -le 5 ]; then
            echo "Fresh telemetry received: age ${age}s."
            return 0
        fi
        attempt=$((attempt + 1))
        [ "$attempt" -lt 15 ] || break
        echo "Waiting for a recent MQTT sample: age ${age}s (limit 5s)."
        sleep 1
    done
    fail "Stale telemetry: last age ${age}s, limit 5s; no network changed"
}
check() {
    [ "$(id -u)" = 0 ] || fail 'Root required'
    for command in ip awk grep wpa_cli wget sleep mkdir cat; do
        command -v "$command" >/dev/null 2>&1 || fail "Missing command: $command"
    done
    # Panel requests carry the address read from getsockname by the protected
    # backend. SSH remains supported; this is not a client-supplied body field.
    endpoint=${CC2_NETWORK_PANEL_LOCAL_IP:-}
    [ -n "$endpoint" ] || endpoint=$(printf '%s\n' "${SSH_CONNECTION:-}" | awk '{print $3}')
    [ "$endpoint" = 192.168.1.107 ] || fail 'Open the panel or SSH at the wired address 192.168.1.107' 
    [ "$(cat /sys/class/net/cc2lan0/carrier)" = 1 ] || fail 'Ethernet carrier absent'
    ip -4 addr show dev cc2lan0 | grep -q 'inet 192.168.1.107/32 ' || fail 'Wired address changed'
    ip -4 addr show dev wlan0 | grep -q 'inet 192.168.1.103/24 ' || fail 'Original Wi-Fi address changed'
    wifi_up || fail 'Original Wi-Fi must be up'
    wpa status | grep -q '^wpa_state=COMPLETED$' || fail 'Original Wi-Fi not connected'
    ip -4 rule show | grep -q '^22030:.*from 192.168.1.107.*lookup 200' || fail 'Qualified source rule absent'
    if ip -4 rule show | grep -q '^22040:'; then fail 'Trial rule priority already in use'; fi
    ip -4 route show table 200 | grep -q '^default via 192.168.1.1 dev cc2lan0' || fail 'Wired Internet route absent'
    ip -4 route show table main | grep -q '^default via 192.168.1.1 dev wlan0' || fail 'Native default route changed'
    read_fresh_printer
    printf '%s\n' "$data" | grep -q '"connected":true' || fail 'Printer disconnected'
    printf '%s\n' "$data" | grep -q '"machine":{"status":1,' || fail 'Printer not Idle'
    printf '%s\n' "$data" | grep -q '"print":{"enabled":false,' || fail 'Print session still enabled'
    for heater in extruder heater_bed; do
        printf '%s\n' "$data" | awk -v k="$heater" -f "$BASE/thermal.awk" || fail "Invalid or unsafe $heater readback; requires temperature <=50 and target zero"
    done
    if printf '%s\n' "$data" | grep -q '"active":true'; then fail 'Background operation active'; fi
    update=$(wget -qO- http://127.0.0.1:8081/api/app-update/status) || fail 'Update state unavailable'
    printf '%s\n' "$update" | grep -q '"busy":false' || fail 'Update active or unknown'
    echo 'CHECK PASS: wired SSH, existing routes, native Wi-Fi, fresh cold Idle state. No network changed.'
}

check
