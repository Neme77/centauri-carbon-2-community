# Complete Connections installer

For the qualified **Community Firmware V4.4.2** runtime. Extract the entire ZIP,
close video viewers and use a connected, idle, cold printer with heater targets
zero. Run `Install-Windows.cmd`, or `sh install-cc2-control.sh` on Linux/macOS.
This computer package includes the application, cumulative camera fix and the
optional network component. It is not a panel-upload ZIP.

Verified hardware: **TP-Link UE300**, Realtek **RTL8153**, USB **0bda:8153**,
device revision **31fd**. Other brands, USB IDs or revisions are not automatically
qualified by sharing a chipset. This adds integrated CC2 support; the stock
firmware does not provide the driver. Negotiated 1000 Mb/s is a link rate, not a
transfer benchmark; the printer uses USB 2.0.

Open Settings → Connections. Wi-Fi remains available without an adapter. Enable
USB Ethernet support only if wanted; choose the connection manually. Saved Wi-Fi
profiles, DHCP/static addresses and interface toggles are available. Network
actions alone omit the LAN-code gate; updates and experimental controls retain it.
There is no signal-quality selection daemon or confirmed-connection test expiry.

After switching, open the selected address and press **Keep this connection**.
Unconfirmed changes restore the previous connection; confirmed choices persist
across reboot. Disable the other connection from the address you intend to keep.
The last enabled interface cannot be disabled. Display-IP interception is excluded.

Installation stages on `/opt/usr`, cleans payloads only after complete success,
and retains logs and application/network rollback backups. Preserve the printed
paths on failure rather than repeatedly reinstalling. It requests no heating,
movement or automatic reboot. Follow any first-time camera activation reboot notice.

The network runtime is byte-identical to accepted R5h: the maintainer confirmed
switching, local/remote video, Orca transfer, reboot persistence and normal Canvas
startup. The newly rebuilt cumulative package needs its own device check. Full
network GPL sources/notices are inside `cc2-connections.tar.gz`; application
sources are in `cc2-control-sources.zip`. No vendor ELF is included.

On first installation and activation of USB Ethernet support, Canvas may flash
red temporarily while the camera/printer services realign. It becomes operational
again when that realignment finishes.
