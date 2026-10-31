# The source is the fingerprint-qualified original set_lan.sh retained locally.
# Replace only LAN subnet selection/firewall handling; preserve all original
# enable/disable commands and their persistent ban_wlan semantics.
/^get_lan_net[(][)][{][ \t]*$/ {
    replacement=1; depth=1; count++
    print "get_lan_net(){ /opt/usr/cc2-network-manager/control --subnets; }"
    next
}
replacement {
    # The original top-level function ends with a brace at column zero.
    if($0 ~ /^}[ \t]*$/) replacement=0
    next
}
/^set_firewall_wlan_net[(][)][{][ \t]*$/ {
    firewall=1; count++
    print "set_firewall_wlan_net(){"
    print "    case \"$1\" in"
    print "        disable)"
    print "            current_lan=$(get_lan_net) || return 1"
    print "            [ -n \"$current_lan\" ] || return 1"
    print "            iptables -F OUTPUT || return 1"
    print "            iptables -A OUTPUT -o lo -j ACCEPT || return 1"
    print "            for net in $current_lan;do iptables -A OUTPUT -d \"$net\" -j ACCEPT || return 1;done"
    print "            iptables -A OUTPUT -j DROP || return 1"
    print "        ;;"
    print "        enable) iptables -F OUTPUT;;"
    print "    esac"
    print "}"
    next
}
firewall { if($0 ~ /^}[ \t]*$/) firewall=0; next }
/^case "\$1" in[ \t]*$/ {
    print
    print "    cc2_refresh_policy)"
    print "        firewall_flag=$(fw_printenv -n ban_wlan)"
    print "        if [ \"$firewall_flag\" = true ];then set_firewall_wlan_net disable;fi"
    print "        exit $?"
    print "    ;;"
    count++
    next
}
{ print }
END {
    if(count!=3 || replacement || firewall) {
        printf "WAN transformation refused: matched=%d expected=3 subnet_open=%d firewall_open=%d\n",count,replacement,firewall > "/dev/stderr"
        exit 1
    }
}
