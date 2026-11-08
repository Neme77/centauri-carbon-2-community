#!/bin/sh
# Back up an official ELEGOO OTA download from your own printer to the USB stick.
#
# Start it yourself over SSH on Community Firmware, BEFORE tapping
# "Check for updates":
#   nohup sh /mnt/exUDISK/ota_grabber.sh >/dev/null 2>&1 &
#   tail -f /mnt/exUDISK/ota_grabber_log.txt
# Stop it with:  kill "$(cat /mnt/exUDISK/ota_grabbed/.pid)"
#
# It only reads printer files and writes only below $USB_ROOT. It does not
# touch the network, the update process or any printer setting. Every regular
# file larger than MIN_KB that appears after the start is copied once its
# size has stayed unchanged for STABLE_ROUNDS checks, and the copy is verified
# against the source with a checksum.
#
# Overridable for local tests: USB_ROOT, WATCH_DIRS, MIN_KB, INTERVAL,
# STABLE_ROUNDS, REQUIRE_MOUNT (1 = USB_ROOT must be a mount point).

USB_ROOT="${USB_ROOT:-/mnt/exUDISK}"
WATCH_DIRS="${WATCH_DIRS:-/opt/usr /tmp /var/tmp /data /userdata /update /upgrade /opt/update /opt/upgrade /opt/download}"
MIN_KB="${MIN_KB:-10240}"
INTERVAL="${INTERVAL:-2}"
STABLE_ROUNDS="${STABLE_ROUNDS:-2}"
REQUIRE_MOUNT="${REQUIRE_MOUNT:-1}"

DEST="$USB_ROOT/ota_grabbed"
STATE="$DEST/.state"
LOG="$USB_ROOT/ota_grabber_log.txt"

log() { printf '%s %s\n' "$(date '+%Y-%m-%d %H:%M:%S')" "$*" >> "$LOG"; }

size_of() { stat -c %s "$1" 2>/dev/null || wc -c < "$1" 2>/dev/null | tr -d ' '; }

checksum() {
    if command -v sha256sum >/dev/null 2>&1; then sha256sum "$1" | cut -d' ' -f1
    elif command -v shasum >/dev/null 2>&1; then shasum -a 256 "$1" | cut -d' ' -f1
    elif command -v md5sum >/dev/null 2>&1; then md5sum "$1" | cut -d' ' -f1
    else echo none; fi
}

key_of() { printf '%s' "$1" | md5sum | cut -c1-32; }   # BusyBox on the CC2 has md5sum but no cksum

# --- Preconditions: never guess a destination on internal storage. ---
if [ ! -d "$USB_ROOT" ] || [ ! -w "$USB_ROOT" ]; then
    echo "STOP: USB stick not writable at $USB_ROOT" >&2; exit 1
fi
if [ "$REQUIRE_MOUNT" = 1 ] && ! awk -v m="$USB_ROOT" '$2==m {ok=1} END {exit !ok}' /proc/mounts 2>/dev/null; then
    echo "STOP: $USB_ROOT is not a mounted USB stick" >&2; exit 1
fi
mkdir -p "$STATE" || { echo "STOP: cannot create $STATE" >&2; exit 1; }
if ! mkdir "$DEST/.lock" 2>/dev/null; then
    echo "STOP: grabber already running (remove $DEST/.lock if not)" >&2; exit 1
fi
echo $$ > "$DEST/.pid"
trap 'rmdir "$DEST/.lock" 2>/dev/null; rm -f "$DEST/.pid"; log "Grabber stopped."; exit 0' INT TERM HUP
MARKER="$DEST/.start-marker"
: > "$MARKER"
sleep 2   # FAT has 2 s timestamp resolution; files older than the start stay excluded

if ! find "$DEST" -xdev -type f -newer "$MARKER" -size +1k >/dev/null 2>&1; then
    log "STOP: this find does not support -xdev/-newer/-size; grabber cannot work here."
    rmdir "$DEST/.lock" 2>/dev/null; rm -f "$DEST/.pid"
    echo "STOP: find lacks -xdev/-newer/-size" >&2; exit 1
fi
log "Grabber started (pid $$). Destination: $DEST"
log "Watching: $WATCH_DIRS (files > ${MIN_KB} KiB created after start)"
df -h 2>/dev/null | sed 's/^/  df: /' >> "$LOG"

copy_file() {
    src="$1"; key="$2"; size="$3"
    name="$(basename "$src")"
    target="$DEST/$name"
    [ -e "$target" ] && target="$DEST/$(date '+%Y%m%d-%H%M%S')_$name"
    log "COPY: $src ($size bytes) -> $target"
    if ! cp "$src" "$target.part" 2>/dev/null; then
        log "ERROR: copy failed or source vanished: $src"; rm -f "$target.part"; return 1
    fi
    sum_src="$(checksum "$src" 2>/dev/null)"
    sum_dst="$(checksum "$target.part")"
    if [ -z "$sum_src" ] || [ "$sum_src" != "$sum_dst" ]; then
        log "ERROR: checksum mismatch (source changed or vanished): src=$sum_src copy=$sum_dst"
        mv -f "$target.part" "$target.INCOMPLETE"
        return 1
    fi
    mv -f "$target.part" "$target"
    sync
    echo "$size $sum_dst" > "$STATE/$key.done"
    log "SAVED: $target ($size bytes) checksum $sum_dst"
}

rounds=0
while :; do
    for dir in $WATCH_DIRS; do
        [ -d "$dir" ] || continue
        find "$dir" -xdev -type f -newer "$MARKER" -size +"${MIN_KB}"k 2>/dev/null
    done | sort -u | while IFS= read -r f; do
        case "$f" in "$USB_ROOT"|"$USB_ROOT"/*) continue ;; esac
        key="$(key_of "$f")"
        size="$(size_of "$f")"
        [ -n "$size" ] || continue
        if [ -f "$STATE/$key.done" ]; then
            read -r done_size _ < "$STATE/$key.done"
            [ "$done_size" = "$size" ] && continue
            rm -f "$STATE/$key.done"
            log "CHANGED after saving, will copy again: $f"
        fi
        last=""; count=0
        [ -f "$STATE/$key" ] && read -r last count _ < "$STATE/$key"
        if [ -z "$last" ]; then
            log "NEW: $f ($size bytes)"; count=0
        elif [ "$last" = "$size" ]; then
            count=$((count + 1))
        else
            log "GROWING: $f ($last -> $size bytes)"; count=0
        fi
        echo "$size $count $f" > "$STATE/$key"
        if [ "$count" -ge "$STABLE_ROUNDS" ]; then
            copy_file "$f" "$key" "$size" && rm -f "$STATE/$key"
        fi
    done
    for st in "$STATE"/*; do
        case "$st" in *.done|"$STATE/*") continue ;; esac
        read -r last _ path < "$st"
        if [ ! -f "$path" ]; then
            log "VANISHED before it was stable (last size $last): $path"
            rm -f "$st"
        fi
    done
    rounds=$((rounds + 1))
    [ $((rounds % 30)) -eq 0 ] && log "alive"
    sleep "$INTERVAL"
done
