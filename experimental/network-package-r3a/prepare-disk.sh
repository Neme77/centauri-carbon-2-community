#!/bin/sh
set -eu
umask 077
[ "$(id -u)" = 0 ] || { echo 'STOP: root required'; exit 1; }
stage=/opt/usr/cc2-control-network-trial-r3
[ ! -e "$stage" ] && [ ! -L "$stage" ] || { echo 'STOP: previous R3 stage exists; preserve its log'; exit 1; }
free=$(df -k /opt/usr | awk 'NR==2 {print $4}')
case "$free" in ''|*[!0-9]*) echo 'STOP: free disk space unavailable'; exit 1;; esac
[ "$free" -ge 65536 ] || { echo 'STOP: less than 64 MiB free on /opt/usr'; exit 1; }
mkdir "$stage"
echo 'Disk staging prepared'
