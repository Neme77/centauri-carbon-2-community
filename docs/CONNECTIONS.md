# Wi-Fi and USB Ethernet Connections

The upcoming cumulative CC2 Control update adds an optional manual Connections
manager. The release remains under qualification; do not install a source archive.

The physically tested runtime profile is Community V4.4.2, kernel 5.4.61 #40
ARMv7, TP-Link UE300 with Realtek RTL8153, USB 0bda:8153 and bcdDevice 31fd.
Other RTL8153 manufacturers, IDs or revisions require separate qualification.
RTL8139, RTL8152 and RTL8156 are not covered by this profile.

Wi-Fi remains available without an adapter. The user chooses whether to enable
USB Ethernet, which connection to use, and DHCP or a static IP. There is no signal
quality switching policy. Unconfirmed connection changes recover the previous
settings; confirmed choices persist across reboot. The existing camera/reactor
patches and native WAN restrictions are preserved. The display-IP interception
experiment is excluded.

The final cumulative package will be tested on the maintainer's printer before
publication. Earlier R5h validation covered switching both directions, local and
remote video, Orca transfer, reboot persistence and normal Canvas startup. This
is separate from final-package qualification. Exact runtime hashes, sources,
licences and rollback instructions are supplied with the installer.
