# Manual Connections manager — diagnostic candidate

This isolated component is a private hardware-validation candidate, not a
published CC2 Control release. The production app is assembled in a disposable
checkout. Canonical sources, builder snapshots and update catalogs are unchanged.

The qualified profile is Community V4.4.2, kernel 5.4.61 ARMv7, the pinned native
Wi-Fi/camera/reactor binaries and UE300 RTL8153 USB revision 31fd. Qualification
refuses other inputs. Proprietary executables are inspected locally, never bundled.

The user manually chooses Wi-Fi/Ethernet, saved/new Wi-Fi profiles, DHCP/static IP,
and enabled interfaces. Ethernet is optional. There is no RSSI policy or standing
preference daemon. A connection change has an independent confirmation worker;
an unconfirmed change restores its predecessor, while a confirmed choice has no
expiry. Failed startup can retain Wi-Fi access without changing the saved choice.

The app submits bounded typed JSON through stdin to the pinned GNU ARM helper.
Secrets are not placed in command arguments, logs or private configuration files.
The original WPA service saves Wi-Fi credentials only when the connection is
confirmed. Native profile IDs are remapped by SSID/priority during rollback.

Read status uses one coalesced asynchronous sample, not recurring child processes.
DHCP callbacks have per-interface generations and cannot confirm an old lease.
Source-routing tables are privately owned; unrelated/main routes are not flushed.
Scoped clients replace the native global DHCP kill only while management is active.
The native WAN restriction remains effective for the selected local networks.

Startup delegates preserve the existing camera R2 and reactor guards and fallback
commands. They do not modify `/etc/init.d/printer`. Activating missing compatibility
hooks requires fresh cold Idle state and an already armed recovery worker. Newly
started native processes are detached from that worker's process group.

Network actions omit the LAN-code gate as requested, retaining shared HTTP
host/origin/request guards. Updates, experiments and Remember LAN retain their
existing authorization. The disk installer keeps logs/backups and removes staged
archives only after both app and network installation succeed.

Qualification includes sanitizers, native WPA datagrams, confirmation-worker
lifecycle, DHCP lifecycle, exact startup transformations on GNU/BusyBox, real HTTP
guards, GNU static ARM/QEMU execution, production UI events and responsive layout,
axe across all app pages, native Windows PowerShell 5.1 transport arguments and
GNU/macOS-style checksum launchers. These do not replace validation of the new
persistent controller on the physical printer. Earlier successful hardware trials
qualified the temporary driver/shim and manual LAN/cloud/Wi-Fi return.
