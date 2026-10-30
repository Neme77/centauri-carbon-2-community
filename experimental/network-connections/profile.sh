#!/bin/sh
# Read-only qualification. Module loading and native restarts are separate steps.
set -eu
umask 077
PATH=/usr/sbin:/usr/bin:/sbin:/bin:/opt/bin
export PATH
ROOT=/opt/usr/cc2-network-manager
fail(){ echo "profile:$*";exit 1; }
sha(){ sha256sum "$1" | awk '{print $1}'; }
[ "$(id -u)" = 0 ] || fail root_required
[ -d "$ROOT" ] && [ ! -L "$ROOT" ] || fail component_missing
(cd "$ROOT" && sha256sum -c SHA256SUMS >/dev/null) || fail component_checksum
[ ! -f "$ROOT/startup.sha256" ] || sha256sum -c "$ROOT/startup.sha256" >/dev/null || fail changed_startup
[ "$(sha /bin/wifi_daemon)" = 8e19a620921c6f4f21bde910cc3d960af5400d223493041eb96f1e921dcbe3c1 ] || fail native_wifi_profile
case "${1:-wifi}" in
 wifi) exit 0;;
 ethernet|boot) ;;
 *) fail unknown_profile;;
esac
[ "$(sha /opt/bin/ai_camera)" = ec5633b17de329d5b18c3f8d8de6ebf389391a04193536d976ba167022d07b99 ] || fail camera_profile
[ "$(sha /opt/usr/cc2-reactor-runtime-v2/elegoo_printer)" = 19712b5943b10bb8cb6aab9a4ed498ee962b28538b23c91b7ed003eb9e590745 ] || fail reactor_program_profile
[ "$(sha /opt/usr/cc2-reactor-runtime-v2/launcher.sh)" = a260865964759898cf47ed8e30e20eb8981506c1a22ada32611b9911b36b47fc ] || fail reactor_launcher_profile
[ "$(sha /etc/init.d/printer)" = e7537298955e23b7a03cab4c8d7bd241c1dfcc070883cdf51c4889cd255d1bc4 ] || fail native_startup_profile
for item in 'libcc2-camera-reap-r2.so 8ef3a5c215462e3c9f32c3d4b85ea23e27c41e330d53b22d7cdeca9d100d016a' 'libcc2-camera-recovery-r2.so 08f2455a99fa5294153ae64ce34117c02f4f8d2dff8527dfd8386aae361a289a' 'cc2-camera-reap-check dce1180e3c04512fea1c4f9868b131f3fbe9bda187ebc9342f9db633f0a57ea0';do
    file=${item%% *};expected=${item#* }
    [ "$(sha "/opt/usr/cc2-control/camera/$file")" = "$expected" ] || fail camera_component_profile
done
[ "$(sha /opt/usr/cc2-reactor-runtime-v2/libcc2-reactor-v2.so)" = 4639364c68489ec40c4e0c4f425550f3aa19e44c2ee61ed641affdc560e7e92c ] || fail reactor_library_profile
[ "$(uname -r)" = 5.4.61 ] && [ "$(uname -m)" = armv7l ] || fail kernel_profile
config=$(zcat /proc/config.gz | sha256sum)
[ "${config%% *}" = 8fa0a87f203d69cb9c4e027e8db11a9a0d92c5d2ea8ae83f87b34714aae7ec4a ] || fail kernel_configuration
[ "$(sha /lib/modules/5.4.61/aic8800_fdrv.ko)" = 2973d7d7856b612ed5880fefa5b8f339d5bd882ff93f44ca47806c83a8a8d8c7 ] || fail original_wifi_driver
exit 0
