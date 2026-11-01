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

## Additional RTL8153 revision (issue #274)

Brands are not the selection criterion. The manager checks USB vendor `0bda`,
product `8153`, device revision and USB high-speed operation. The physically
accepted `31fd` path remains available without additional consent.

Revision `3000`, reported by **kalross**, can be tried only after an explicit
experimental approval in Connections. Approval alone neither loads modules nor
changes the selected connection. Ethernet activation still requires the native
kernel/profile checks, a matching revision-specific ABI probe, exact module
checksums and a single matching USB device bound to `cc2lan0`. A manually loaded
driver does not bypass the probe. Other revisions and multiple RTL8153 adapters
remain refused. RTL8139, RTL8152 and RTL8156 are not enabled by this change.

Consent is stored separately from the network configuration and confirmation
journal. Disabling Ethernet permits consent to be revoked, including after the
adapter is unplugged. Connection confirmation and timeout recovery are unchanged.
Experimental approval is not hardware qualification: reset, suspend, reboot,
long printing and local/remote camera operation on `3000` still require a device
owner's validation. No stable catalog entry is published by the preview build.
