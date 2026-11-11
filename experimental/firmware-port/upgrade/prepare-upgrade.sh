set -eu
umask 077
stop() { echo "STOP: $*" >&2; exit 1; }
check_path() {
    if [ "$1" = cc2-reactor-disabled ]; then
        [ -f "$1" ] && [ ! -L "$1" ] && [ ! -s "$1" ] || stop invalid_reactor_disable_marker
        LC_ALL=C ls -ldn "$1" | awk 'NR==1{ok=($1~/^-/&&$2==1&&$3==0)}END{exit !(NR==1&&ok)}' || stop unsafe_reactor_disable_marker
        return
    fi
    [ "$1" = cc2-rfid ] ||
        printf '%s\n' "$1" | grep -Eq '^cc2-control-(backup-[0-9]{8}-[0-9]{6}-[0-9]+|install-[0-9a-f]{32})$' || stop invalid_target
    [ -d "$1" ] && [ ! -L "$1" ] && [ "$(cd "$1" && pwd -P)" = "/opt/usr/$1" ] || stop "target_changed $1"
}
canvas_evidence() {
    case "$evidence" in /tmp/cc2-upgrade-evidence-*) ;; *) stop invalid_evidence_probe_path;; esac
    [ -f "$evidence" ] && [ ! -L "$evidence" ] || stop missing_canvas_evidence_probe
    [ "$(sha256sum "$evidence" | cut -d ' ' -f 1)" = "$evidence_sha" ] || stop canvas_evidence_probe_changed
    chmod 700 "$evidence"
    canvas_state=$("$evidence" --inspect) || stop canvas_evidence_not_verified
    case "$canvas_state" in ABSENT|DORMANT|RESTORED) ;; *) stop invalid_canvas_evidence_reply;; esac
    printf 'Canvas preparation state: %s\n' "$canvas_state" >&2
}

ready() {
    printer=$(pidof elegoo_printer 2>/dev/null || :)
    case "$printer" in ''|*[!0-9]*) stop printer_process_unknown;; esac
    for binary in /opt/bin/elegoo_printer "/proc/$printer/exe"; do
        [ "$(sha256sum "$binary" | cut -d ' ' -f 1)" = 19712b5943b10bb8cb6aab9a4ed498ee962b28538b23c91b7ed003eb9e590745 ] || stop unqualified_printer_profile
    done
    [ "$(sha256sum /opt/inst/daemon-000/daemon-000 | cut -d ' ' -f 1)" = 83433b14e49d1e8aa9523282ead03618378573f4b66de1ad8f7f3c444b11aada ] || stop unqualified_daemon_profile
    "$probe" --installer-native-idle >&2 || stop printer_not_idle_cold
    [ ! -L cc2-rfid ] || stop unknown_canvas_state
    for f in cc2-rfid/lifecycle.next cc2-rfid/.lifecycle-*.next cc2-rfid/activation-request cc2-bed-drying.session; do
        if [ -e "$f" ] || [ -L "$f" ]; then stop "review_pending_state $f"; fi
    done
    canvas_evidence
    case "$canvas_state" in
        DORMANT|RESTORED)
            case " $* " in *' cc2-rfid '*) ;; *) stop verified_canvas_missing_from_plan;; esac;;
        ABSENT)
            case " $* " in *' cc2-rfid '*) stop canvas_state_changed;; esac;;
    esac

}
main() {
    [ "$(id -u)" = 0 ] || stop root_required
    cd /opt/usr
    [ "$(pwd -P)" = /opt/usr ] || stop unexpected_root
    case "$action" in
    discover)
        for path in cc2-control-backup-* cc2-control-install-*; do
            [ -d "$path" ] && [ ! -L "$path" ] || continue
            printf '%s\n' "$path" | grep -E '^cc2-control-(backup-[0-9]{8}-[0-9]{6}-[0-9]+|install-[0-9a-f]{32})$' || :
        done
        if [ -e cc2-reactor-disabled ] || [ -L cc2-reactor-disabled ]; then
            echo cc2-reactor-disabled
        fi
        canvas_evidence
        case "$canvas_state" in DORMANT|RESTORED) echo cc2-rfid;; esac
        if [ "$evidence_keep" = 0 ]; then rm -f "$evidence"; fi
        echo 'PLAN COMPLETE'
        return ;;
    esac
    [ "$#" -gt 0 ] || stop empty_plan
    lock=/tmp/cc2-control-install.lock
    mkdir "$lock" || stop installer_busy
    trap 'if [ "$action" = apply ]; then rm -f "$probe" "$evidence"; fi; rmdir "$lock"' 0
    trap 'exit 1' HUP INT TERM
    [ -f "$probe" ] && [ ! -L "$probe" ] || stop missing_probe
    [ "$(sha256sum "$probe" | cut -d ' ' -f 1)" = "$probe_sha" ] || stop probe_changed
    chmod 700 "$probe"
    ready "$@"
    for path do
        check_path "$path"
        full="/opt/usr/$path"
        if awk -v p="$full" '$2==p || index($2,p "/")==1 {found=1} END {exit !found}' /proc/mounts; then stop "mounted_target $path"; fi
        for proc in /proc/[0-9]*; do
            for link in "$proc/exe" "$proc/cwd" "$proc/root" "$proc"/fd/*; do
                resolved=$(readlink "$link" 2>/dev/null || :)
                case "$resolved" in "$full"|"$full/"*) stop "target_in_use $path";; esac
            done
            if grep -F "$full/" "$proc/maps" >/dev/null 2>&1; then stop "mapped_target $path"; fi
        done
    done
    case "$action" in
    backup) tar -cf - "$@" ;;
    apply)
        set -o pipefail
        digest=$(tar -cf - "$@" | sha256sum) || stop archive_read_failed
        [ "${digest%% *}" = "$archive_sha" ] || stop targets_changed_since_backup
        ready "$@"
        dest=$(mktemp -d /opt/usr/.cc2-upgrade-quarantine.XXXXXX) || stop quarantine_failed
        printf 'QUARANTINE=%s\n' "$dest"
        for path do
            check_path "$path"
            mv "$path" "$dest/" || stop "move_failed $path"
            printf 'MOVED=%s\n' "$path"
        done
        sync
        echo 'QUARANTINE COMPLETE' ;;
    *) stop unknown_action ;;
    esac
}
main "$@"
