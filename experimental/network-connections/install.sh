#!/bin/sh
# Install only the qualified component and reversible delegates. No network,
# heater, motion, module loading, native restart or reboot is requested here.
set -eu
umask 077
PATH=/usr/sbin:/usr/bin:/sbin:/bin
export PATH
SOURCE=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd -P)
ROOT=/opt/usr/cc2-network-manager
STATE=/opt/usr/cc2-network-state
STAGE=/opt/usr/.cc2-network-manager-$$
BACKUP=/opt/usr/cc2-network-backup-$(date +%Y%m%d-%H%M%S)-$$
COMMITTED=0
BACKUP_READY=0
INSTALL_PHASE=initial
phase(){ INSTALL_PHASE=$1;printf 'CONNECTIONS_INSTALL_PHASE=%s\n' "$INSTALL_PHASE"; }
finish(){
    code=$?
    trap - 0 1 2 15
    printf 'CONNECTIONS_INSTALL_EXIT=%s LAST_PHASE=%s\n' "$code" "$INSTALL_PHASE"
    if [ "$code" -ne 0 ] && [ "$COMMITTED" = 0 ] && [ "$BACKUP_READY" = 1 ];then
        if sh "$BACKUP/restore.sh" --installer;then
            echo 'CONNECTIONS_RESTORE_EXIT=0'
        else
            restore_code=$?
            printf 'CONNECTIONS_RESTORE_EXIT=%s\n' "$restore_code"
            echo 'RESTORE INCOMPLETE: inspect retained backup'
        fi
    fi
    exit "$code"
}
trap finish 0
trap 'exit 1' 1 2 15
fail(){ echo "STOP: $*";exit 1; }
sha(){ sha256sum "$1" | awk '{print $1}'; }
[ "$(id -u)" = 0 ] || fail root_required
[ -d /opt/usr ] && [ ! -L /opt/usr ] || fail persistent_storage
[ "$SOURCE" != "$ROOT" ] && [ ! -e "$STAGE" ] && [ ! -L "$STAGE" ] || fail staging_path
phase payload-checksums
(cd "$SOURCE" && sha256sum -c SHA256SUMS) || fail payload_checksums
phase printer-preflight
"$SOURCE/control" --guard 8081 || fail printer_not_ready
phase previous-session-preflight
"$SOURCE/control" --legacy-idle || fail finish_previous_network_trial
phase native-profile
for item in '/etc/init.d/printer e7537298955e23b7a03cab4c8d7bd241c1dfcc070883cdf51c4889cd255d1bc4' '/bin/wifi_daemon 8e19a620921c6f4f21bde910cc3d960af5400d223493041eb96f1e921dcbe3c1' '/opt/usr/cc2-control/camera/boot-camera.sh e570b84e4b5615bfea53be5c52a1befef7b78f0b91ba16e62b500652a8cdfaa5' '/opt/usr/cc2-reactor-runtime-v2/launcher.sh a260865964759898cf47ed8e30e20eb8981506c1a22ada32611b9911b36b47fc';do
    file=${item%% *};expected=${item#* }
    [ -f "$file" ] && [ ! -L "$file" ] && [ "$(sha "$file")" = "$expected" ] || fail native_profile
done
phase stage-copy
mkdir "$STAGE" "$BACKUP"
cp -a "$SOURCE/." "$STAGE/"
chmod 700 "$STAGE" "$BACKUP"
phase retain-native-hooks
if [ -e "$ROOT" ] || [ -L "$ROOT" ];then
    [ -d "$ROOT" ] && [ ! -L "$ROOT" ] || fail existing_component
    "$ROOT/control" --busy && fail pending_network_change
    (cd "$ROOT" && sha256sum -c SHA256SUMS >/dev/null) || fail existing_component_checksums
    sha256sum -c "$ROOT/startup.sha256" >/dev/null || fail changed_startup
    cp "$ROOT/native-udhcpc_wlan0" "$STAGE/native-udhcpc_wlan0"
    cp "$ROOT/native-set_lan.sh" "$STAGE/native-set_lan.sh"
    cp "$ROOT/native-camera-launch" "$STAGE/native-camera-launch"
    cp "$ROOT/native-reactor-launch" "$STAGE/native-reactor-launch"
else
    for item in '/opt/usr/wifi/udhcpc_wlan0 b76f68e345f1221d835d1a0d6279827c5df4823c2e7fadfe0298cd49ec7bb7df' '/usr/share/udhcpc/set_lan.sh 53cf6af55ad333d5781b0d48d89fd48bde22bc516bd22e76d45f098dc224443c' '/etc/init.d/cc2-camera-launch f05accc97f0195cf4cf481b53b643902424183bb335a27f42e6069a625813c6f' '/etc/init.d/cc2-reactor-launch 8d4a19c9be1f6f85fe5ab9d3f116aa84400e7e6a77c455e66da636373fdaac43';do
        file=${item%% *};expected=${item#* }
        [ -f "$file" ] && [ ! -L "$file" ] && [ "$(sha "$file")" = "$expected" ] || fail existing_native_hook
    done
    cp /opt/usr/wifi/udhcpc_wlan0 "$STAGE/native-udhcpc_wlan0"
    cp /usr/share/udhcpc/set_lan.sh "$STAGE/native-set_lan.sh"
    cp /etc/init.d/cc2-camera-launch "$STAGE/native-camera-launch"
    cp /etc/init.d/cc2-reactor-launch "$STAGE/native-reactor-launch"
fi
phase qualify-native-wifi-entry
# wifi_daemon invokes /etc/wifi, a distinct file on the qualified firmware.
# Older manager revisions replaced only /opt/usr/wifi and left a global DHCP
# kill active. Accept only the stock file or our previously recorded delegate.
[ -d /etc/wifi ] && [ ! -L /etc/wifi ] && [ -f /etc/wifi/udhcpc_wlan0 ] && [ ! -L /etc/wifi/udhcpc_wlan0 ] || fail native_wifi_entry_type
if [ "$(sha /etc/wifi/udhcpc_wlan0)" != b76f68e345f1221d835d1a0d6279827c5df4823c2e7fadfe0298cd49ec7bb7df ];then
    [ -d "$ROOT" ] && [ -f "$ROOT/startup.sha256" ] && grep -F '  /etc/wifi/udhcpc_wlan0' "$ROOT/startup.sha256" >/dev/null || fail native_wifi_entry_profile
    [ "$(sha /etc/wifi/udhcpc_wlan0)" = "$(sha "$ROOT/wifi-client.sh")" ] || fail native_wifi_entry_profile
fi
phase prepare-camera-entry
awk -v mode=camera-entry -f "$STAGE/prepare-hooks.awk" /opt/usr/cc2-control/camera/boot-camera.sh > "$STAGE/camera-entry.sh" || fail camera_entry_transform
phase prepare-reactor-entry
awk -v mode=reactor-entry -f "$STAGE/prepare-hooks.awk" /opt/usr/cc2-reactor-runtime-v2/launcher.sh > "$STAGE/reactor-entry.sh"
phase prepare-startup-delegates
awk -v mode=camera-launch -f "$STAGE/prepare-hooks.awk" "$STAGE/native-camera-launch" > "$STAGE/camera-launch.sh"
awk -v mode=reactor-launch -f "$STAGE/prepare-hooks.awk" "$STAGE/native-reactor-launch" > "$STAGE/reactor-launch.sh"
phase prepare-wan-policy
awk -f "$STAGE/native-wan-policy.awk" "$STAGE/native-set_lan.sh" > "$STAGE/native-wan.sh" || fail wan_policy_transform
phase validate-shell
for f in "$STAGE/"*.sh;do sh -n "$f" || fail generated_shell_syntax;done
for path in /etc/init.d/cc2-network-manager /etc/rc.d/S23cc2-network-manager /etc/hotplug.d/net/90-cc2-network-manager;do
    if [ -e "$path" ] || [ -L "$path" ];then
        [ -d "$ROOT" ] || fail existing_foreign_hook
    fi
done
phase backup-hooks
mkdir "$BACKUP/files"
for path in /etc/init.d/printer /opt/usr/wifi/udhcpc_wlan0 /etc/wifi/udhcpc_wlan0 /usr/share/udhcpc/set_lan.sh /etc/init.d/cc2-camera-launch /etc/init.d/cc2-reactor-launch /etc/init.d/cc2-network-manager /etc/rc.d/S23cc2-network-manager /etc/hotplug.d/net/90-cc2-network-manager;do
    if [ -e "$path" ] || [ -L "$path" ];then
        mkdir -p "$BACKUP/files$(dirname "$path")"
        cp -a "$path" "$BACKUP/files$path"
    else printf '%s\n' "$path" >> "$BACKUP/absent";fi
done
cp "$STAGE/rollback.sh" "$BACKUP/restore.sh"
BACKUP_READY=1
phase install-component
if [ -d "$ROOT" ];then mv "$ROOT" "$BACKUP/component";fi
mv "$STAGE" "$ROOT"
chmod 700 "$ROOT/control" "$ROOT/"*.sh
phase normalize-native-permissions
# Keep the qualified contents unchanged. The native script ships group-writable
# (0775), while the runtime gate requires no group/world write permission.
# Its exact previous mode is retained by cp -a in the rollback backup above.
[ -f /etc/init.d/printer ] && [ ! -L /etc/init.d/printer ] && [ "$(sha /etc/init.d/printer)" = e7537298955e23b7a03cab4c8d7bd241c1dfcc070883cdf51c4889cd255d1bc4 ] || fail native_startup_before_chmod
chmod 755 /etc/init.d/printer || fail native_startup_permissions
phase install-delegates
cp "$ROOT/wifi-client.sh" /opt/usr/wifi/udhcpc_wlan0
cp "$ROOT/wifi-client.sh" /etc/wifi/udhcpc_wlan0
cp "$ROOT/native-lease.sh" /usr/share/udhcpc/set_lan.sh
cp "$ROOT/camera-launch.sh" /etc/init.d/cc2-camera-launch
cp "$ROOT/reactor-launch.sh" /etc/init.d/cc2-reactor-launch
cp "$ROOT/init.sh" /etc/init.d/cc2-network-manager
cp "$ROOT/hotplug.sh" /etc/hotplug.d/net/90-cc2-network-manager
chmod 755 /opt/usr/wifi/udhcpc_wlan0 /etc/wifi/udhcpc_wlan0 /usr/share/udhcpc/set_lan.sh /etc/init.d/cc2-camera-launch /etc/init.d/cc2-reactor-launch /etc/init.d/cc2-network-manager /etc/hotplug.d/net/90-cc2-network-manager
if [ ! -L /etc/rc.d/S23cc2-network-manager ];then ln -s ../init.d/cc2-network-manager /etc/rc.d/S23cc2-network-manager;fi
phase verify-installation
sha256sum /opt/usr/wifi/udhcpc_wlan0 /etc/wifi/udhcpc_wlan0 /usr/share/udhcpc/set_lan.sh /etc/init.d/cc2-camera-launch /etc/init.d/cc2-reactor-launch /etc/init.d/cc2-network-manager /etc/hotplug.d/net/90-cc2-network-manager > "$ROOT/startup.sha256"
(cd "$ROOT";find . -type f ! -name SHA256SUMS | LC_ALL=C sort | while IFS= read -r f;do sha256sum "$f";done) > "$ROOT/SHA256SUMS"
sh "$ROOT/profile.sh" ethernet || fail installed_profile
# Defaults leave native Wi-Fi ownership untouched. No helper/module is started.
phase seed-configuration
"$ROOT/control" --seed || fail configuration_seed
COMMITTED=1
phase complete
sync
echo "CONNECTIONS INSTALLED. Backup: $BACKUP"
echo 'No service restarted and no Ethernet module loaded. Use Commuta in the panel.'
