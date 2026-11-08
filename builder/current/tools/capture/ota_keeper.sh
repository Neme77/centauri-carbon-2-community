#!/bin/sh
# Keep an official ELEGOO OTA download on your own printer by hard-linking it,
# and copy it to the USB stick when possible.
#
# The online update writes the package to /opt/usr/firmware/ and reboots into
# the installer about a minute later, which can be too short to copy ~114 MB
# to a USB stick. A hard link in the same file system takes no time and no
# space; when the updater later removes its own name, the data stays
# reachable under KEEP_DIR. /opt/usr survived stock reflashes in issue #309.
#
# Watch mode (default). Start it over SSH on Community Firmware, BEFORE tapping
# "Check for updates" (BusyBox on the CC2 has no nohup):
#   sh -c 'trap "" HUP; exec sh /mnt/exUDISK/ota_keeper.sh' >/dev/null 2>/mnt/exUDISK/ota_keeper_err.txt &
#   tail -f /mnt/exUDISK/ota_keeper_log.txt
# Every new file is hard-linked at once. Once a file stops growing it is also
# copied to USB_DIR and verified with sha256sum; the outer package first.
#
# Export mode (after a later Community Firmware flash, if the copy did not
# finish before the reboot):
#   sh /mnt/exUDISK/ota_keeper.sh export
#
# Stop watch mode with:  kill "$(cat /opt/usr/ota_keep/.pid)"
# It never modifies, moves or deletes files written by the updater. Its only
# writes on the printer are hard links and its own log/pid/state files.
# Overridable for local tests: WATCH_DIR, KEEP_DIR, USB_ROOT, LOG, INTERVAL.

WATCH_DIR="${WATCH_DIR:-/opt/usr/firmware}"
KEEP_DIR="${KEEP_DIR:-/opt/usr/ota_keep}"
USB_ROOT="${USB_ROOT:-/mnt/exUDISK}"
USB_DIR="$USB_ROOT/ota_keep_copy"
LOG="${LOG:-$USB_ROOT/ota_keeper_log.txt}"
INTERVAL="${INTERVAL:-1}"
OUTER_NAME_FILE="$KEEP_DIR/.outer_name"

log() {
    line="$(date '+%Y-%m-%d %H:%M:%S') $*"
    printf '%s\n' "$line" >> "$KEEP_DIR/keeper.log"
    printf '%s\n' "$line" >> "$LOG" 2>/dev/null
    printf '%s\n' "$line"
}

size_of() { wc -c < "$1" 2>/dev/null | tr -d ' '; }

# Name the outer package after the file name stored in its ELEG header
# (bytes 0x10..0x8f, e.g. cc2_eeb001_02.01.01.13_<stamp>.zip -> .zip.sig).
usb_name() {
    src="$1"; name="$(basename "$src")"
    if [ "$name" = elegoo_firmware ]; then
        embedded="$(dd if="$src" bs=1 skip=16 count=128 2>/dev/null | tr -d '\000' | tr -cd 'A-Za-z0-9._-')"
        case "$embedded" in
            *.zip) name="$embedded.sig"; echo "$name" > "$OUTER_NAME_FILE" ;;
        esac
    fi
    printf '%s' "$name"
}

usb_ready() { [ -d "$USB_ROOT" ] && [ -w "$USB_ROOT" ] && awk -v m="$USB_ROOT" '$2==m {ok=1} END {exit !ok}' /proc/mounts 2>/dev/null; }
[ "${REQUIRE_MOUNT:-1}" = 1 ] || usb_ready() { [ -d "$USB_ROOT" ] && [ -w "$USB_ROOT" ]; }

# Copy one kept file to the stick and verify it; returns 0 only when verified.
copy_to_usb() {
    src="$1"
    usb_ready || { log "USB stick not available at $USB_ROOT; kept file stays in $KEEP_DIR"; return 1; }
    mkdir -p "$USB_DIR" || return 1
    name="$(usb_name "$src")"
    target="$USB_DIR/$name"
    size="$(size_of "$src")"
    if [ -f "$target" ] && [ "$(size_of "$target")" = "$size" ] && [ -f "$target.sha256" ]; then
        if [ "$(sha256sum "$src" | cut -d' ' -f1)" = "$(cut -d' ' -f1 < "$target.sha256")" ]; then
            return 0
        fi
    fi
    log "COPY: $src ($size bytes) -> $target"
    if ! cp "$src" "$target.part"; then
        log "ERROR: copy failed: $src"; rm -f "$target.part"; return 1
    fi
    sync
    sum_src="$(sha256sum "$src" | cut -d' ' -f1)"
    sum_dst="$(sha256sum "$target.part" | cut -d' ' -f1)"
    if [ "$sum_src" != "$sum_dst" ]; then
        log "ERROR: checksum mismatch: src=$sum_src copy=$sum_dst"; rm -f "$target.part"; return 1
    fi
    mv -f "$target.part" "$target"
    printf '%s  %s\n' "$sum_dst" "$name" > "$target.sha256"
    sync
    log "SAVED ON USB: $target ($size bytes) sha256 $sum_dst"
}

# Outer package first: it is the one input the builder needs.
kept_files() {
    [ -f "$KEEP_DIR/elegoo_firmware" ] && printf '%s\n' "$KEEP_DIR/elegoo_firmware"
    for f in "$KEEP_DIR"/*; do
        [ -f "$f" ] || continue
        case "$(basename "$f")" in elegoo_firmware|keeper.log) continue ;; esac
        printf '%s\n' "$f"
    done
}

mkdir -p "$KEEP_DIR" || { echo "STOP: cannot create $KEEP_DIR" >&2; exit 1; }

if [ "${1:-}" = export ]; then
    log "Export started: $KEEP_DIR -> $USB_DIR"
    failed=0
    for f in $(kept_files); do
        copy_to_usb "$f" || failed=1
    done
    if [ "$failed" = 0 ]; then
        log "EXPORT COMPLETE. Files on USB:"
        ls -la "$USB_DIR" | tee -a "$LOG"
    else
        log "EXPORT INCOMPLETE; see messages above. Kept files are untouched."
    fi
    exit "$failed"
fi

# A pid file from before a reboot must not block a new start.
if [ -f "$KEEP_DIR/.pid" ]; then
    old="$(cat "$KEEP_DIR/.pid")"
    if [ -n "$old" ] && grep -q ota_keeper "/proc/$old/cmdline" 2>/dev/null; then
        echo "STOP: keeper already running (pid $old)" >&2; exit 1
    fi
fi
echo $$ > "$KEEP_DIR/.pid"
rmdir "$KEEP_DIR/.copying" 2>/dev/null   # left over if a reboot interrupted a copy
trap 'rm -f "$KEEP_DIR/.pid"; log "Keeper stopped."; exit 0' INT TERM HUP

# Prove that a hard link from WATCH_DIR's file system to KEEP_DIR works.
probe_parent="$(dirname "$WATCH_DIR")"
: > "$probe_parent/.ota_keeper_probe" 2>/dev/null
if ! ln "$probe_parent/.ota_keeper_probe" "$KEEP_DIR/.probe" 2>/dev/null; then
    rm -f "$probe_parent/.ota_keeper_probe"
    log "STOP: hard links from $probe_parent to $KEEP_DIR are not possible (different file system?)"
    rm -f "$KEEP_DIR/.pid"; exit 1
fi
rm -f "$probe_parent/.ota_keeper_probe" "$KEEP_DIR/.probe"

usb_ready && log "Keeper started (pid $$). Watching $WATCH_DIR, links in $KEEP_DIR, copies to $USB_DIR" \
          || log "Keeper started (pid $$) WITHOUT USB stick; links only in $KEEP_DIR"

while :; do
    # 1. Link every new file at once.
    for f in "$WATCH_DIR"/* "$WATCH_DIR"/.[!.]*; do
        [ -f "$f" ] && [ ! -L "$f" ] || continue
        name="$(basename "$f")"
        inode="$(ls -i "$f" 2>/dev/null | awk '{print $1}')"
        [ -n "$inode" ] || continue
        [ -f "$KEEP_DIR/.inode_$inode" ] && continue
        target="$KEEP_DIR/$name"
        [ -e "$target" ] && target="$KEEP_DIR/$(date '+%Y%m%d-%H%M%S')_$name"
        if ln "$f" "$target" 2>/dev/null; then
            echo "$target" > "$KEEP_DIR/.inode_$inode"
            log "LINKED: $f -> $target (inode $inode, size now $(size_of "$f") bytes)"
        else
            log "ERROR: could not link $f"
        fi
    done
    # 2. Copy kept files that stopped growing and are not on the stick yet.
    #    One background copy at a time, so linking (step 1) never waits.
    if mkdir "$KEEP_DIR/.copying" 2>/dev/null; then
        started=0
        for f in $(kept_files); do
            state="$KEEP_DIR/.size_$(basename "$f")"
            size="$(size_of "$f")"
            [ -n "$size" ] && [ "$size" != 0 ] || continue
            [ -f "$state.done" ] && [ "$(cat "$state.done")" = "$size" ] && continue
            if [ -f "$state" ] && [ "$(cat "$state")" = "$size" ]; then
                ( copy_to_usb "$f" && echo "$size" > "$state.done"; rmdir "$KEEP_DIR/.copying" ) &
                started=1
                break
            fi
            echo "$size" > "$state"
        done
        [ "$started" = 1 ] || rmdir "$KEEP_DIR/.copying"
    fi
    sleep "$INTERVAL"
done
