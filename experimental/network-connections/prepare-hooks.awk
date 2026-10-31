# Build startup delegates from exact qualified inputs without editing vendor
# printer init or the checksummed camera/reactor runtime payloads.
mode == "camera-entry" && /^CC2_CAMERA_REAP_TRIAL=1 .*exec "\$CAMERA" "\$@"$/ {
    print "if /opt/usr/cc2-network-manager/control --enabled;then"
    print "    /opt/usr/cc2-network-manager/control --ensure || true"
    line=$0
    sub(/LD_PRELOAD="/, "CC2_NETWORK_COMPAT_TRIAL=1 LD_PRELOAD=\"/opt/usr/cc2-network-manager/libcc2-network-compat-r1.so:", line)
    print "    " line
    print "fi"
    count++
}
mode == "reactor-entry" && /^export LD_PRELOAD="\$BASE\/libcc2-reactor-v2.so"$/ {
    # External helpers must run before LD_PRELOAD is exported. The reactor's
    # constructor intentionally refuses every executable except elegoo_printer.
    print "CC2_NETWORK_STARTUP=0"
    print "if /opt/usr/cc2-network-manager/control --enabled;then"
    print "    /opt/usr/cc2-network-manager/control --ensure || true"
    print "    CC2_NETWORK_STARTUP=1"
    print "fi"
    print
    print "if [ \"$CC2_NETWORK_STARTUP\" = 1 ];then"
    print "    export CC2_NETWORK_COMPAT_TRIAL=1"
    print "    export LD_PRELOAD=/opt/usr/cc2-network-manager/libcc2-network-compat-r1.so:$LD_PRELOAD"
    print "fi"
    count++
    next
}
mode == "camera-launch" && /^exec "\$BASE\/boot-camera.sh" "\$@"$/ {
    print "if /opt/usr/cc2-network-manager/control --enabled;then exec /opt/usr/cc2-network-manager/camera-entry.sh \"$@\";fi"
    count++
}
mode == "reactor-launch" && /^exec "\$BASE\/launcher.sh" "\$@"$/ {
    print "if /opt/usr/cc2-network-manager/control --enabled;then exec /opt/usr/cc2-network-manager/reactor-entry.sh \"$@\";fi"
    count++
}
{ print }
END { if(count!=1) exit 1 }
