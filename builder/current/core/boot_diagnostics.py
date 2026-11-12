"""Bounded boot status, readable even when the vendor image has no logread."""


def shell_note():
    return '''cc2_boot_note() {
    case "$1" in reactor|extensions|camera) ;; *) return 0;; esac
    [ "$(id -u)" = 0 ] || return 0
    [ -d /opt/usr ] && [ ! -L /opt/usr ] || return 0
    grep -q ' /opt/usr ' /proc/mounts || return 0
    cc2_status=/opt/usr/cc2-$1-boot.status
    [ ! -L "$cc2_status" ] && { [ ! -e "$cc2_status" ] || [ -f "$cc2_status" ]; } || return 0
    cc2_tmp=$(mktemp /opt/usr/.cc2-boot-status.XXXXXX) || return 0
    printf '%s\\n' "$2" >"$cc2_tmp" && chmod 600 "$cc2_tmp" &&
        mv -f "$cc2_tmp" "$cc2_status" || { rm -f "$cc2_tmp";return 0; }
}
'''
