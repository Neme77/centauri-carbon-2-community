#!/bin/sh
# Retire ONLY the exact inactive R1/R1a Windows trials's seven staged files.
set -eu
umask 077
BASE=/opt/usr/cc2-cavity-floor
fail(){ echo "STOP: $*" >&2;exit 1; }
[ "$(id -u)" = 0 ] || fail root_required
[ -d /opt/usr ] && [ ! -L /opt/usr ] || fail unsafe_persistent_parent
if [ ! -e "$BASE" ] && [ ! -L "$BASE" ];then echo 'NO_PREVIOUS_STAGING: nothing changed.';exit 0;fi
[ -d "$BASE" ] && [ ! -L "$BASE" ] || fail unsafe_component
[ ! -e "$BASE/enabled" ] && [ ! -L "$BASE/enabled" ] || fail component_enabled
[ ! -e "$BASE/backup" ] && [ ! -L "$BASE/backup" ] || fail component_preflight_started
[ ! -e "$BASE/lock" ] && [ ! -L "$BASE/lock" ] || fail component_locked
for file in "$BASE"/* "$BASE"/.[!.]* "$BASE"/..?*;do
    [ -e "$file" ] || [ -L "$file" ] || continue
    name=$(basename "$file")
    case "$name" in
        SHA256SUMS|launcher.sh|libcc2-cavity-floor-r1.so|manage.sh|profile.original.sha256|profile.sh|thermal.awk) ;;
        *) fail foreign_staging_entry;;
    esac
    [ -f "$file" ] && [ ! -L "$file" ] || fail unsafe_staged_file
done
manifest=$(sha256sum "$BASE/SHA256SUMS" | awk '{print $1}')
case "$manifest" in
    b893a42e84056fc93c3dfef824f1fe903733417d3ae97a68b72da6b8a32001c3) reference=6e3a0c2f7850e487d2a89b1b3d964a17f3dc1513e076b8c02e013d0c634589ac;;
    d882c225a1a69ca98f1f82434c58387eee761a003a7a08d2d7af1270217d86b9) reference=63441c9f3600e739cf88c63a24a2e28f973901f1f03563effff0c849b27395af;;
    *) fail unrecognized_staging_manifest;;
esac
sha256sum -c - <<EXPECTED
$manifest  /opt/usr/cc2-cavity-floor/SHA256SUMS
67ef9d561671968fa2c1ac37af9b241d7b100e7f470f28dec1fa8e04e365a09d  /opt/usr/cc2-cavity-floor/launcher.sh
52d400605dd51f89eb95248c42a1661b3c0e60fadec5453d27a987729332fb10  /opt/usr/cc2-cavity-floor/libcc2-cavity-floor-r1.so
d3b23f3afd059028823f7f003621e991c4d0ffd1705e10dfb06ec249372de008  /opt/usr/cc2-cavity-floor/manage.sh
$reference  /opt/usr/cc2-cavity-floor/profile.original.sha256
3525aa6aa9e1b3038d2e1793bc6e0a6f7941f00f3824987e2a4e8620427c0e0e  /opt/usr/cc2-cavity-floor/profile.sh
1cb4534a5c6b447cfd8dd963c125614115effeb4d469b61e0eb0f50469f2d749  /opt/usr/cc2-cavity-floor/thermal.awk
EXPECTED
for maps in /proc/[0-9]*/maps;do
    if grep -F -q "$BASE/libcc2-cavity-floor-r1.so" "$maps" 2>/dev/null;then fail component_in_use;fi
done
ARCHIVE=/opt/usr/cc2-cavity-floor-staging-before-retry-$(date +%Y%m%d-%H%M%S)-$$
[ ! -e "$ARCHIVE" ] && [ ! -L "$ARCHIVE" ] || fail archive_collision
mv "$BASE" "$ARCHIVE"
echo "ARCHIVED_INACTIVE_STAGING=$ARCHIVE"
echo 'No file deleted, service restarted, app updated or active startup file changed.'
