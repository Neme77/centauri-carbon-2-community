#!/bin/sh /etc/rc.common
# One-shot, detached boot worker. No persistent network preference daemon.
START=23
STOP=98
start(){ /opt/usr/cc2-network-manager/control --ensure; }
boot(){ start; }
