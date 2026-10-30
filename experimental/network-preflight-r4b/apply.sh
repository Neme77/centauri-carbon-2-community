#!/bin/sh
set -eu
umask 077
BASE=/tmp/cc2-network-compat-r1
WORK=/opt/usr/cc2-network-preflight-r4b
fail(){ echo "STOP: $*" >&2; exit 1; }
sha(){ sha256sum "$1" | awk '{print $1}'; }
[ "$(id -u)" = 0 ] || fail 'Root required'
[ -d "$BASE" ] && [ ! -L "$BASE" ] && [ -d "$BASE/source" ] && [ ! -L "$BASE/source" ] || fail 'Original trial not staged'
for name in preflight.sh source/preflight.sh SHA256SUMS manage.sh;do
    [ -f "$BASE/$name" ] && [ ! -L "$BASE/$name" ] || fail 'Unexpected trial file'
done
[ "$(sha "$BASE/manage.sh")" = 5f548f24b150eda793a2e7c712a8e6f7a601f2238517b591660ed2df753df0c4 ] || fail 'Trial manager differs'
[ "$(sha "$BASE/SHA256SUMS")" = 6446000364dee0c07be28a9016e5a002959bdfd9df52eeaedc9301e922184e39 ] || fail 'Original manifest differs; no file changed'
[ "$(sha "$BASE/preflight.sh")" = f36be51aeee50947d9125067473ef1e174e06e1df8d965a3a311caff7d0eb6c9 ] || fail 'Original preflight differs'
[ "$(sha "$BASE/source/preflight.sh")" = f36be51aeee50947d9125067473ef1e174e06e1df8d965a3a311caff7d0eb6c9 ] || fail 'Original preflight source differs'
(cd "$BASE" && sha256sum -c SHA256SUMS) || fail 'Original payload checksum failed'
# A mapped shared library may stay mapped: this correction changes no library.
# Refuse changes while any trial script or watchdog is executing.
for item in /proc/[0-9]*/cmdline;do
    if [ -r "$item" ] && tr '\0' ' ' < "$item" 2>/dev/null | grep -F "$BASE/manage.sh" >/dev/null;then
        fail 'Trial action or watchdog running; wait for native restoration'
    fi
done
[ ! -e "$WORK" ] && [ ! -L "$WORK" ] || fail 'Prior correction exists; preserve its backup'
mkdir "$WORK"
cp -p "$BASE/preflight.sh" "$WORK/preflight.before"
cp -p "$BASE/source/preflight.sh" "$WORK/source-preflight.before"
cp -p "$BASE/SHA256SUMS" "$WORK/SHA256SUMS.before"
started=0
committed=0
rollback(){
    result=$?
    trap - 0 1 2 15
    if [ "$started" = 1 ] && [ "$committed" = 0 ];then
        cp -p "$WORK/preflight.before" "$BASE/preflight.sh" &&
        cp -p "$WORK/source-preflight.before" "$BASE/source/preflight.sh" &&
        cp -p "$WORK/SHA256SUMS.before" "$BASE/SHA256SUMS" &&
        echo 'Original preflight restored after correction failure.' >&2
    fi
    exit "$result"
}
trap rollback 0
trap 'exit 130' 1 2 15
cat > "$WORK/preflight.new" <<'CC2_PREFLIGHT_R4B'
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
# A local UDS read must finish even on BusyBox without timeout or working wget -T.
read_completed_uds() (
    umask 077
    read_dir="$BASE/status-read.$$"
    mkdir "$read_dir" || exit 1
    reader=
    cleanup_read() {
        trap - 0 1 2 15
        if [ -n "$reader" ]; then kill -KILL "$reader" 2>/dev/null || true; fi
        rm -f "$read_dir/reply"
        rmdir "$read_dir"
    }
    trap cleanup_read 0
    trap 'exit 130' 1 2 15
    wget -qO- http://127.0.0.1:8081/api/uds > "$read_dir/reply" 2>/dev/null &
    reader=$!
    seconds=0
    while kill -0 "$reader" 2>/dev/null; do
        [ "$seconds" -lt 5 ] || exit 124
        sleep 1
        seconds=$((seconds + 1))
    done
    if wait "$reader"; then reader=; cat "$read_dir/reply"; else reader=; exit 1; fi
)
print_session_idle() {
    if printf '%s\n' "$data" | grep -q '"print":{"enabled":false,'; then return 0; fi
    printf '%s\n' "$data" | grep -q '"print":{"enabled":true,' || fail 'Print session state unavailable'
    printf '%s\n' "$data" | grep -q '"native":{"state":"complete",' || fail 'Print session still enabled; native completion not confirmed'
    uds=$(read_completed_uds) || fail 'Native completion read unavailable or timed out'
    printf '%s\n' "$uds" | grep -q '^[{]"connected":true,"fresh":true,' || fail 'Fresh connected UDS completion required'
    printf '%s\n' "$uds" | grep -q '"print_state":"complete"' || fail 'Native print is not complete'
    echo 'Completed print confirmed by fresh UDS; retained MQTT enabled flag accepted.'
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
    print_session_idle
    for heater in extruder heater_bed; do
        printf '%s\n' "$data" | awk -v k="$heater" -f "$BASE/thermal.awk" || fail "Invalid or unsafe $heater readback; requires temperature <=50 and target zero"
    done
    if printf '%s\n' "$data" | grep -q '"active":true'; then fail 'Background operation active'; fi
    update=$(wget -qO- http://127.0.0.1:8081/api/app-update/status) || fail 'Update state unavailable'
    printf '%s\n' "$update" | grep -q '"busy":false' || fail 'Update active or unknown'
    echo 'CHECK PASS: wired SSH, existing routes, native Wi-Fi, fresh cold Idle state. No network changed.'
}

check
CC2_PREFLIGHT_R4B
cat > "$WORK/SHA256SUMS.new" <<'CC2_MANIFEST_R4B'
ec763a0de50654a6886d3e8aa81ddac8237c4d3548b3644cdfb576e69e1d2ca7  README.it.md
bde512195dd6bf3846f3103b5e1acbd89a62e5ee819e405389c97f5fc188ff3a  libcc2-network-compat-r1.so
5f548f24b150eda793a2e7c712a8e6f7a601f2238517b591660ed2df753df0c4  manage.sh
2cb547486992ce5095d24ea3cf74f560d47c8bddca9c3043651579a9ad79348b  offline-validation.json
2e32607ba854c8d7014755425290d7e5d6aeb95a888508473dd26b7af047c7a2  preflight.sh
2ca0d8d4dbdc015fe3bc9960638067e551c31271c45adaf95213ec21bb016aa0  provenance.json
d0e8bd6a3b30327a18fbf1e9851a3deb0d5101ba95bf1f98aa74aff2d0e67c38  required-symbol-versions.txt
3972dc9744f6499f0f9b2dbf76696f2ae7ad8af9b23dde66d6af86c9dfb36986  source/LICENSE
3822e3824336789b331c03302f45ef76f361db49102ec37b53b3dffb8f9c0e91  source/README.md
bb33d049e46d55a577bfb9594e3f43007710e5231bfd2b1ddcdb809f7766c86c  source/abi_call.c
b47e15b65a36cf121e84202fb692d7d96208af6dc2f661971cff87fa204ed67f  source/build-ci.py
5f548f24b150eda793a2e7c712a8e6f7a601f2238517b591660ed2df753df0c4  source/manage.sh
52858e57d3de39a31c6ade383c80027126782374a217d00c532f8d44a9bdf1a7  source/net_core.c
318461a44d3d67fda9c851a2510c7e7d9b32266745cd9a878c3c74922226bd08  source/net_core.h
f8a4d434675ad121d4ec830ba7a5b8fac845810ecd227e8a89d29f88509e04d4  source/network_compat.c
2e32607ba854c8d7014755425290d7e5d6aeb95a888508473dd26b7af047c7a2  source/preflight.sh
65dd5495b5320fbc4caadbcb455b85df8f43935d0b59589906ce1b3d433dcdad  source/sha256_file.c
93dfa0fcfca1e3b685aa6eb9680176f2ed53e579d9aa569cef2fc1309dc30641  source/sha256_file.h
ba9d6b235d8fe7d329ab43be60aa3c1b18d9da80b460f4e1592e97189b184b02  source/test_abi.cpp
4f04cb7769a9ee64956fdc79899ed47b4bf41ab488c072572fbdfa2e0343dee4  source/test_core.c
ca0491fae700b744af919d9c667edce46a2c01ba90a1062e22b2fc4762b15c32  source/test_hooks_arm.c
07921243451fb1aedd4411539005d7265d6ad87d139ce8ef4371310772f53e7e  source/test_trial.py
1cb4534a5c6b447cfd8dd963c125614115effeb4d469b61e0eb0f50469f2d749  source/thermal.awk
9ff6e098a3f62ced82e7d8727cc8ba26524a430af366413e6a2805763083d50a  source/thermal_selftest.sh
1cb4534a5c6b447cfd8dd963c125614115effeb4d469b61e0eb0f50469f2d749  thermal.awk
9ff6e098a3f62ced82e7d8727cc8ba26524a430af366413e6a2805763083d50a  thermal_selftest.sh
CC2_MANIFEST_R4B
[ "$(sha "$WORK/preflight.new")" = 2e32607ba854c8d7014755425290d7e5d6aeb95a888508473dd26b7af047c7a2 ] || fail 'New preflight checksum failed'
[ "$(sha "$WORK/SHA256SUMS.new")" = 7ec9c2be4280f0f8086eff28906ab272be355b276976bf9f005a3eb457f8a141 ] || fail 'New manifest checksum failed'
sh -n "$WORK/preflight.new" || fail 'New script syntax failed'
cp "$WORK/preflight.new" "$BASE/preflight.r4b.new"
cp "$WORK/preflight.new" "$BASE/source/preflight.r4b.new"
cp "$WORK/SHA256SUMS.new" "$BASE/SHA256SUMS.r4b.new"
chmod 600 "$BASE/preflight.r4b.new" "$BASE/source/preflight.r4b.new" "$BASE/SHA256SUMS.r4b.new"
started=1
mv "$BASE/preflight.r4b.new" "$BASE/preflight.sh"
mv "$BASE/source/preflight.r4b.new" "$BASE/source/preflight.sh"
mv "$BASE/SHA256SUMS.r4b.new" "$BASE/SHA256SUMS"
(cd "$BASE" && sha256sum -c SHA256SUMS) || fail 'Corrected payload verification failed'
committed=1
printf '%s\n' 'R4B PREFLIGHT PATCH PASSED. No service restarted; no network changed.' | tee "$WORK/result.log"
echo 'Return to the wired panel and choose Prepare trial.'
