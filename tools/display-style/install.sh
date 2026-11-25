#!/bin/sh
set -eu
umask 077
ROOT=''
BASE=$(CDPATH='' cd "$(dirname "$0")" && pwd)
MODE=${1:---check}
BACKUP="$BASE/backup"
GUI=opt/bin/ec-eeb001-gui
LIB=opt/inst/cc2-native-display/libcc2-native-display.so
ROWS="$BASE/files.txt"
fail(){ echo "STOP: $*; preserve this kit and report" >&2; exit 125; }
safe(){
 path=$1
 while [ -n "$path" ]; do
  [ ! -L "$path" ] || fail "symlink: $path"
  [ "$path" != / ] || break
  path=${path%/*}
 done
}
digest(){ sha256sum "$1" | awk '{print $1}'; }
hash(){ safe "$2"; [ -f "$2" ] || fail "missing file: $2"; [ "$(digest "$2")" = "$1" ] || fail "checksum: $2"; }
state(){ if [ -f "$BASE/state" ]; then cat "$BASE/state"; else echo NEW; fi; }
rows(){ cat "$ROWS"; }
replace(){
 safe "$2"
 temporary=$(mktemp "$2.cc2-style.XXXXXX") || return 1
 if cp "$1" "$temporary" && chmod "$3" "$temporary" && mv -f "$temporary" "$2"; then return 0; fi
 rm -f "$temporary";return 1
}
snapshot(){
 safe "$ROOT/opt/usr/cc2-rfid"
 if [ ! -e "$ROOT/opt/usr/cc2-rfid" ]; then printf 'RFID_DIRECTORY_ABSENT\n' > "$1"; return; fi
 "$BASE/rfid-evidence" > "$1.unsorted" || return 1
 sort "$1.unsorted" > "$1"
}
verify_unchanged(){
 snapshot "$AUDIT/rfid.after" || fail rfid_evidence_unavailable
 [ "$(digest "$AUDIT/rfid.before")" = "$(digest "$AUDIT/rfid.after")" ] || fail RFID_CHANGED
 echo RFID_FILES_UNCHANGED
}
restore_files(){
 # Restore the GUI first; an unmatched dependency refuses hooks until the
 # library is restored next. No display process is restarted by this script.
 rows | while read -r rel old new mode source; do
  replace "$BACKUP/$rel" "$ROOT/$rel" "$mode" || exit 1
  hash "$old" "$ROOT/$rel"
 done
}
case "$MODE" in --check|--apply|--restore|--status) ;; *) fail 'use --check, --apply, --restore or --status';; esac
[ "$(id -u)" = 0 ] || fail root_required
safe "$BASE";safe "$BACKUP";safe "$BASE/state"
hash d509c56c641e4ed0bd8fac037ad4e0d5038b170b8fb1495f10d7946e77f7c358 "$ROWS"
hash b5b2f1653a70bd84252d4e2285e01f9e87c0a4918ab9c5b4da75a3655bcb4e90 "$BASE/rfid-evidence"
if [ "$MODE" = --status ]; then echo "DISPLAY_STYLE_STATE=$(state)"; exit 0; fi
# Check the running release by immutable slot identity, not the version text alone.
active="$ROOT/opt/usr/cc2-panel-releases/active"
safe "$active";[ -f "$active" ] || fail managed_panel_absent
[ "$(cat "$active")" = '6c805fe9c9d4beaa7c2c5354bf08660890be33979ff889bc5d22aba01acbed06' ] || fail unsupported_panel_release
hash 6c805fe9c9d4beaa7c2c5354bf08660890be33979ff889bc5d22aba01acbed06 "$ROOT/opt/usr/cc2-panel-releases/6c805fe9c9d4beaa7c2c5354bf08660890be33979ff889bc5d22aba01acbed06/cc2-control"
hash 768a9e8c307d2398dc486be3a3ca8dfa6e9265677627d133f03b6914a1a34e59 "$ROOT/opt/bin/elegoo_printer"
helper="$ROOT/opt/inst/cc2-control/cc2-control"
safe "$helper";[ -f "$helper" ] && [ -x "$helper" ] || fail native_guard_absent
case "$(digest "$helper")" in 29cba60c7ac3e46368d58ad16f59e89c21b17c4b88c87e1021c7f8162d4b55d1|1cfce43da4c161e194f3cfa5636d62b782b1269abbb6856be59521edc150175f) ;; *) fail unsupported_native_guard;; esac
if [ "$MODE" = --restore ]; then
 safe "$BACKUP/ready";[ -f "$BACKUP/ready" ] || fail backup_not_ready
 [ "$(cat "$BACKUP/ready")" = 'd509c56c641e4ed0bd8fac037ad4e0d5038b170b8fb1495f10d7946e77f7c358' ] || fail backup_manifest_mismatch
else
 [ "$(state)" = NEW ] || fail 'prior operation exists; use Restore, do not repeat Apply'
 [ ! -e "$BACKUP" ] || fail retain_existing_backup
fi
rows | while read -r rel old new mode source; do
 case "$rel" in opt/bin/ec-eeb001-gui|opt/inst/cc2-native-display/libcc2-native-display.so|opt/inst/images/theme_default/*.png|opt/usr/images/theme_default/*.png) ;; *) fail invalid_target;; esac
 case "$rel:$source" in *..*) fail invalid_path;; esac
 hash "$new" "$BASE/payload/$source"
 if [ "$MODE" = --restore ]; then
  hash "$old" "$BACKUP/$rel"
  safe "$ROOT/$rel";[ -f "$ROOT/$rel" ] || fail missing_restore_target
  current=$(digest "$ROOT/$rel")
  [ "$current" = "$old" ] || [ "$current" = "$new" ] || fail "changed since trial: $rel"
 else hash "$old" "$ROOT/$rel";fi
done
AUDIT=$(mktemp -d "$BASE/audit.XXXXXX") || fail audit_creation
snapshot "$AUDIT/rfid.before" || fail rfid_evidence_unavailable
if [ "$MODE" = --check ]; then
 verify_unchanged
 echo STOCK_STYLE_DISPLAY_READ_ONLY_PASSED
 echo 'Private stage/audit only. No active display file changed or service restarted.'
 exit 0
fi
"$helper" --installer-native-idle || fail printer_not_idle_cold
safe "$ROOT/opt/usr/.cc2-display-install.lock"
mkdir "$ROOT/opt/usr/.cc2-display-install.lock" || fail display_installation_locked
changing=0
cleanup(){
 result=$?;trap - 0 HUP INT TERM
 if [ "$changing" = 1 ]; then
  if restore_files; then
   printf 'RESTORED_AFTER_FAILURE\n' > "$BASE/state"
  else
   echo 'STOP: automatic file restoration incomplete; keep backup, do not reboot' >&2
   result=125
  fi
 fi
 rmdir "$ROOT/opt/usr/.cc2-display-install.lock" || result=125
 exit "$result"
}
trap cleanup 0
trap 'exit 125' HUP INT TERM
if [ "$MODE" = --restore ]; then
 restore_files
 verify_unchanged
 printf 'RESTORED\n' > "$BASE/state"
 sync
 echo COMMUNITY_DISPLAY_STYLE_RESTORED
 echo 'No service restarted. Reboot the cold idle printer once; retain kit and backup.'
 exit 0
fi
# Persist and verify EVERY old file before the first active replacement.
mkdir -m 700 "$BACKUP"
rows | while read -r rel old new mode source; do
 mkdir -p "$BACKUP/${rel%/*}"
 cp -p "$ROOT/$rel" "$BACKUP/$rel"
 hash "$old" "$BACKUP/$rel"
done
printf '%s\n' 'd509c56c641e4ed0bd8fac037ad4e0d5038b170b8fb1495f10d7946e77f7c358' > "$BACKUP/ready"
printf 'PREPARED\n' > "$BASE/state"
sync
changing=1
# Keep the old running GUI mapped. Library/resources are written first, GUI last.
rows | while read -r rel old new mode source; do
 [ "$rel" = "$GUI" ] && continue
 replace "$BASE/payload/$source" "$ROOT/$rel" "$mode" || exit 1
 hash "$new" "$ROOT/$rel"
done
replace "$BASE/payload/gui" "$ROOT/$GUI" 755
rows | while read -r rel old new mode source; do hash "$new" "$ROOT/$rel";done
verify_unchanged
printf 'APPLIED\n' > "$BASE/state"
sync
changing=0
echo STOCK_STYLE_DISPLAY_APPLIED
echo 'No service restarted. Reboot the cold idle printer once; retain kit and backup.'
