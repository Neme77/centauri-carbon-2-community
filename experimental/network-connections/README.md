# Manual Wi-Fi and USB Ethernet Connections

Persistent optional Connections manager integrated into CC2 Control. The qualified
runtime is Community V4.4.2, kernel 5.4.61 #40 ARMv7 and TP-Link UE300 RTL8153
(0bda:8153, revision 31fd). Sharing a chipset does not qualify another USB profile.
The native Wi-Fi service, WAN restrictions, camera R2 and reactor protections
remain active. No vendor executable is redistributed.

The accepted R5h helper/runtime is pinned by runtime.json and the app pin.h.
Corresponding GPL sources are included in the immutable runtime archive. The
source copies consumed by the app and standalone tests are checked for equality.
The application can read network status without the optional manager installed;
network mutations require the exact installed helper. Wi-Fi works without an
adapter. Confirmed choices persist; only unconfirmed changes expire and recover.
No RSSI-based selection or display-IP interception is included.

Run build_public.py as root inside an isolated Linux test environment with GCC,
the GNU ARM compiler, QEMU, BusyBox, dash, sha256sum and shasum. It executes only
fixture sockets/processes and temporary directories, not real printer services.
The Connections qualification workflow also exercises actual HTTP/UI controls,
persistent confirmation feedback and accessibility across the application.

Use package_cumulative.py with the current normal cumulative application ZIP
and the exact runtime from runtime.json. It uses the established disk runner,
Windows/OpenSSH and GNU/macOS checksum paths, retaining logs/rollback backups
and removing staged archives only after complete success. No local workspace or
old app payload is required. package.py, integrate.py and build-ci.py are retained
as historical diagnostic build references, not the public packaging entrypoint.
See README.en.md for installation and compatibility details.

The maintainer physically confirmed the earlier R5h runtime; the newly rebuilt
cumulative app and its package have separate automated/device validation status.
