# Network panel: read-only status qualification

First live-data step after the approved Wi-Fi/LAN layout. This is not a
connection editor or persistent network service. The branch also contains an
experimental adapter and UI that can be assembled in a disposable app checkout.

The ARM32 static executable reads `wlan0` and `cc2lan0`, IPv4 addresses,
interface flags, carrier, byte counters and the native WPA control socket.
It sends only local `STATUS` and (when connected) `SIGNAL_POLL` commands.
It does not read Wi-Fi configuration/passwords, load modules, restart services,
change interface state, save preferences or send Internet traffic.

A UDP `connect` without sending a packet obtains the kernel-selected source
for `1.1.1.1`, including policy routing. This does not verify Internet access.
DHCP/static origin remains unknown: an observed address is not proof of DHCP.
Missing, malformed or duplicate fields are reported as unavailable/unknown.
Status and signal reads record error stage, errno, reply length, attempts and
elapsed time. Only a transient timeout/interruption/resource-busy result gets
one bounded retry; the first failure remains visible even if the retry succeeds.
This is resilience and diagnostics, not a claim to have diagnosed an earlier
intermittent failure on the physical printer. A traced subsequent hardware read
received valid STATUS and SIGNAL_POLL responses through the abstract socket.
The network library mode and its presence in the two services are observations,
not a claim that the cloud, video, routing or compatibility hooks work.

`build-ci.py` checks parsers and actual local WPA datagrams under sanitizers,
builds an ARMv7 hard-float static executable, runs it under QEMU and records
native system calls. The standalone R1b reader has been verified on the printer: native WPA status
and signal returned at the first attempt. The initial R1 failure was not
reproduced and remains undiagnosed. Embedded HTTP/UI qualification is separate.

Production code must additionally coordinate the native global DHCP kill,
default-route/DNS replacement, independent transaction rollback and user WAN
restrictions before enabling edits. The automatic quality policy is not part
of this status reader. The approved preview remains under local outputs.

## Integrated UI trial

`integrate-status.py` and `integrate-ui.py` assemble the read-only API and
Settings tab using the actual project components and nine dictionaries. The
Ethernet visibility switch is a browser preference; it does not load drivers.
Network editing and mode buttons are explicitly disabled in this trial.

The HTTP reader is on demand and asynchronous, with one bounded-stack detached
worker and a three-second minimum sample interval. Leaving the tab stops
browser polling. Failed reads cannot keep an apparently live IP in the UI.

`transaction.h` is tested policy only. A privileged independent worker, a
persisted rollback journal, native DHCP/DNS coordination, optional boot
activation and automatic selection are still required before enabling writes.
The pure policy tests do not qualify hardware recovery or persistent settings.
