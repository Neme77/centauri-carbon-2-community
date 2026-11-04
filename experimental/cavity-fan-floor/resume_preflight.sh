#!/bin/sh
# Resume ONLY the exact interrupted R1b preflight. No active target is written.
set -eu
umask 077
BASE=/opt/usr/cc2-cavity-floor
RUNTIME=/opt/usr/cc2-reactor-runtime-v2
NETWORK=/opt/usr/cc2-network-manager
BACKUP=$BASE/backup
LOCK=$BASE/lock
HELD=0
fail(){ echo "STOP: $*" >&2;exit 1; }
sha(){ sha256sum "$1" | awk '{print $1}'; }
regular(){
    [ -f "$1" ] && [ ! -L "$1" ] || return 1
    LC_ALL=C ls -ldn "$1" 2>/dev/null | awk 'NR==1{ok=($1~/^-/&&$2=="1"&&$3=="0")} END{exit !(NR==1&&ok)}'
}
finish(){
    code=$?;trap - 0 1 2 15
    if [ "$HELD" = 1 ];then
        rm -f "$LOCK/expected-reactor" "$LOCK/network-proposed" "$LOCK/plan"
        rmdir "$LOCK" 2>/dev/null || true
    fi
    exit "$code"
}
trap finish 0
trap 'exit 130' 1 2 15
target(){
    case "$1" in
        reactor-launcher) echo "$RUNTIME/launcher.sh";;
        reactor-manifest) echo "$RUNTIME/SHA256SUMS";;
        network-profile) echo "$NETWORK/profile.sh";;
        network-manifest) echo "$NETWORK/SHA256SUMS";;
        *) return 1;;
    esac
}
rewrite(){
    awk -v name="$2" -v old="$3" -v new="$4" '
        $2==name || $2=="./" name {
            if($1!=old)exit 1
            count++
            if(!sub("^" old,new))exit 1
            print;next
        }
        {print}
        END {if(count!=1)exit 1}
    ' "$1"
}
[ "$(id -u)" = 0 ] || fail root_required
for directory in "$BASE" "$BACKUP" "$RUNTIME" "$NETWORK";do
    [ -d "$directory" ] && [ ! -L "$directory" ] || fail unsafe_directory
done
[ ! -e "$BASE/enabled" ] && [ ! -L "$BASE/enabled" ] || fail already_enabled
[ ! -e "$BACKUP/plan" ] && [ ! -L "$BACKUP/plan" ] || fail plan_already_exists
mkdir "$LOCK" || fail operation_locked
HELD=1
sha256sum -c - <<'EXPECTED'
84b48f37fd2d6f433bb258c4aea0d30ddbd1a5f546ce0367f5081a11c405a7b4  /opt/usr/cc2-cavity-floor/SHA256SUMS
67ef9d561671968fa2c1ac37af9b241d7b100e7f470f28dec1fa8e04e365a09d  /opt/usr/cc2-cavity-floor/launcher.sh
52d400605dd51f89eb95248c42a1661b3c0e60fadec5453d27a987729332fb10  /opt/usr/cc2-cavity-floor/libcc2-cavity-floor-r1.so
5ba7036f7b868b27de5796496339fd1c1b10228c0f0503cc010fe36ec4a58a22  /opt/usr/cc2-cavity-floor/manage.sh
63441c9f3600e739cf88c63a24a2e28f973901f1f03563effff0c849b27395af  /opt/usr/cc2-cavity-floor/profile.original.sha256
3525aa6aa9e1b3038d2e1793bc6e0a6f7941f00f3824987e2a4e8620427c0e0e  /opt/usr/cc2-cavity-floor/profile.sh
1cb4534a5c6b447cfd8dd963c125614115effeb4d469b61e0eb0f50469f2d749  /opt/usr/cc2-cavity-floor/thermal.awk
EXPECTED
for file in "$BASE"/*;do
    case "$(basename "$file")" in
        backup|lock) ;;
        SHA256SUMS|launcher.sh|libcc2-cavity-floor-r1.so|manage.sh|profile.original.sha256|profile.sh|thermal.awk) regular "$file" || fail unsafe_payload_file;;
        *) fail foreign_component_entry;;
    esac
done
count=0
for file in "$BACKUP"/* "$BACKUP"/.[!.]* "$BACKUP"/..?*;do
    [ -e "$file" ] || [ -L "$file" ] || continue
    case "$(basename "$file")" in
        reactor-launcher.old|reactor-launcher.new|reactor-manifest.old|reactor-manifest.new|network-profile.old|network-profile.new|network-manifest.old|network-manifest.new|manifest.next) ;;
        *) fail foreign_backup_entry;;
    esac
    regular "$file" || fail unsafe_backup_file
    count=$((count+1))
done
[ "$count" = 9 ] || fail incomplete_backup_shape
for key in reactor-launcher reactor-manifest network-profile network-manifest;do
    file=$(target "$key")
    regular "$file" && [ "$(sha "$file")" = "$(sha "$BACKUP/$key.old")" ] || fail active_target_changed
done
[ "$(sha "$BACKUP/reactor-launcher.old")" = a260865964759898cf47ed8e30e20eb8981506c1a22ada32611b9911b36b47fc ] || fail unknown_original_launcher
[ "$(sha "$BACKUP/network-profile.old")" = "$(cat "$BASE/profile.original.sha256")" ] || fail unknown_original_profile
[ "$(sha "$BACKUP/reactor-launcher.new")" = "$(sha "$BASE/launcher.sh")" ] || fail changed_launcher_draft
[ "$(sha "$BACKUP/network-profile.new")" = "$(sha "$BASE/profile.sh")" ] || fail changed_profile_draft
for file in "$BACKUP/network-manifest.new" "$BACKUP/manifest.next";do
    [ "$(sha "$file")" = "$(sha "$BACKUP/network-manifest.old")" ] || fail unexpected_interrupted_draft
done
for maps in /proc/[0-9]*/maps;do
    if grep -F -q "$BASE/libcc2-cavity-floor-r1.so" "$maps" 2>/dev/null;then fail component_in_use;fi
done
rewrite "$BACKUP/reactor-manifest.old" launcher.sh "$(sha "$BACKUP/reactor-launcher.old")" "$(sha "$BASE/launcher.sh")" >"$LOCK/expected-reactor" || fail original_reactor_manifest_format
[ "$(sha "$LOCK/expected-reactor")" = "$(sha "$BACKUP/reactor-manifest.new")" ] || fail changed_reactor_manifest_draft
cp -p "$BACKUP/network-manifest.new" "$LOCK/network-proposed"
rewrite "$BACKUP/network-manifest.old" profile.sh "$(sha "$BACKUP/network-profile.old")" "$(sha "$BASE/profile.sh")" >"$LOCK/network-proposed" || fail original_network_manifest_format
: >"$LOCK/plan"
for key in reactor-launcher reactor-manifest network-profile network-manifest;do
    file=$(target "$key")
    [ "$(sha "$file")" = "$(sha "$BACKUP/$key.old")" ] || fail active_target_changed_during_resume
    if [ "$key" = network-manifest ];then proposed=$LOCK/network-proposed;else proposed=$BACKUP/$key.new;fi
    printf '%s %s %s\n' "$(sha "$BACKUP/$key.old")" "$(sha "$proposed")" "$key" >>"$LOCK/plan"
done
[ ! -e "$BACKUP/plan" ] && [ ! -L "$BACKUP/plan" ] || fail plan_created_concurrently
mv -f "$LOCK/network-proposed" "$BACKUP/network-manifest.new"
mv "$LOCK/plan" "$BACKUP/plan"
echo 'PREFLIGHT PREPARED: exact original targets verified; active profile path retained. No active target, app, service or connection changed.'
echo 'Keep the backup. Retry the existing R1b installer only while idle and cold.'
