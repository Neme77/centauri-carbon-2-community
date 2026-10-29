# The API emits compact heater objects followed by other JSON lines.
# Ignore lines without the requested heater, but require exactly one valid one.
{
    p = index($0, "\"" k "\":{")
    if (!p) next
    found++
    if (found != 1) exit 1
    s = substr($0, p)
    sub(/}.*/, "", s)
    if (s !~ /"temperature":[0-9]+([.][0-9]+)?,"target":[0-9]+([.][0-9]+)?$/) exit 1
    sub(/^.*"temperature":/, "", s)
    split(s, a, ",\"target\":")
    if (a[1]+0 > 50 || a[2]+0 != 0) exit 1
}
END { if (found != 1) exit 1 }
