#!/bin/sh
# Tests only fixture strings and awk: no interface, route or heater changes.
set -eu
BASE=$(CDPATH= cd "$(dirname "$0")" && pwd)
AWK_FILE=$BASE/thermal.awk
fail() { echo "SELFTEST FAIL: $*" >&2; exit 1; }
expect_pass() {
    printf '%s\n' "$2" | awk -v k="$1" -f "$AWK_FILE" || fail "Valid $1 fixture rejected"
}
expect_fail() {
    if printf '%s\n' "$2" | awk -v k="$1" -f "$AWK_FILE"; then fail "Unsafe $1 fixture accepted"; fi
}
cold='{"extruder":{"temperature":26.4,"target":0.0},"heater_bed":{"temperature":22.9,"target":0.0}
}'
# Reproduce the R1/R1b defect against the original awk control flow.
if printf '%s\n' "$cold" | awk -v k=extruder '
{p=index($0,"\"" k "\":{");if(!p)exit 1;s=substr($0,p);sub(/}.*/,"",s);
 if(s !~ /"temperature":[0-9.]+,"target":[0-9.]+$/)exit 1;
 sub(/^.*"temperature":/,"",s);split(s,a,",\"target\":");
 if(a[1]+0>50||a[2]+0!=0)exit 1;found=1}END{if(!found)exit 1}'; then
    fail 'Original multiline failure did not reproduce'
fi
expect_pass extruder "$cold"
expect_pass heater_bed "$cold"
expect_pass extruder '{"extruder":{"temperature":50,"target":0}}'
expect_pass extruder '{"extruder":{"temperature":26.4,"target":0.0}}'
expect_fail extruder '{"extruder":{"temperature":50.1,"target":0}}'
expect_fail heater_bed '{"heater_bed":{"temperature":22.9,"target":60}}'
expect_fail extruder '{"extruder":{"temperature":26.4,"target":0.1}}'
expect_fail extruder '{"heater_bed":{"temperature":22.9,"target":0}}'
expect_fail extruder '{"extruder":{"temperature":26.4}}'
expect_fail extruder '{"extruder":{"temperature":"26.4","target":0}}'
expect_fail extruder '{"extruder":{"temperature":26.4,"target":"0"}}'
expect_fail extruder '{"extruder":{"temperature":..,"target":0}}'
expect_fail extruder '{"extruder":{"temperature":26.4,"target":0}}
{"extruder":{"temperature":26.4,"target":0}}'
echo 'SELFTEST PASS: original multiline defect reproduced; 13 thermal fixtures checked. No network changed.'
