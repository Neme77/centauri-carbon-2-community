# Network panel: read-only status qualification

First live-data step after the approved Wi-Fi/LAN layout. This is not a
connection editor, installer or persistent network service.

The ARM32 static executable reads `wlan0` and `cc2lan0`, IPv4 addresses,
interface flags, carrier, byte counters and the native WPA control socket.
It sends only local `STATUS` and (when connected) `SIGNAL_POLL` commands.
It does not read Wi-Fi configuration/passwords, load modules, restart services,
change interface state, save preferences or send Internet traffic.

A UDP `connect` without sending a packet obtains the kernel-selected source
for `1.1.1.1`, including policy routing. This does not verify Internet access.
DHCP/static origin remains unknown: an observed address is not proof of DHCP.
Missing, malformed or duplicate fields are reported as unavailable/unknown.
The network library mode and its presence in the two services are observations,
not a claim that the cloud, video, routing or compatibility hooks work.

`build-ci.py` checks parsers and actual local WPA datagrams under sanitizers,
builds an ARMv7 hard-float static executable, runs it under QEMU and records
native system calls. Real printer qualification is separate and still pending.

Production code must additionally coordinate the native global DHCP kill,
default-route/DNS replacement, independent transaction rollback and user WAN
restrictions before enabling edits. The automatic quality policy is not part
of this status reader. The approved preview remains under local outputs.
