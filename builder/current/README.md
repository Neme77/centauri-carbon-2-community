# Current complete firmware builder

This is the private hardware-test candidate for **Community Firmware 4.4.3**
with **CC2 Control 1.2.25**, based on the exact official CC2 **02.01.01.07** OTA.
The complete new OTA has not yet passed printer validation. Do not publish it or
activate a stable update catalogue until that validation is recorded.

## Integrated components

- The current cumulative CC2 Control application, including the experimental
  nozzle/chamber features, native maintenance tools, Pressure Advance, quick
  actions and update recovery. Experimental features retain their opt-in gates.
- The reviewed native Obsidian/cyan display, original printer/Canvas artwork,
  native callbacks and language support, and inline LAN information.
- The qualified Connections manager and unchanged RTL8153 kernel modules,
  supplied by the firmware rather than requiring a separate trial installer.
- Existing Dual Trust, SSH, Z offset, HTTP/upload, camera coexistence, qualified
  callback bridge and persistent camera R2 integration.

The kernel remains byte-for-byte the qualified stock kernel. Firmware-shipped
out-of-tree modules are **not** a newly compiled or built-in kernel driver.
There is no unconditional Ethernet module autoload: user-enabled support and
USB/ABI qualification still precede loading. A fresh configuration keeps native
Wi-Fi active and Ethernet support disabled until the user enables it.

The physically tested profile is TP-Link UE300, Realtek RTL8153, USB
`0bda:8153`, revision `31fd`. Revision `3000` remains an explicit experimental
opt-in and is not physically qualified by fixture tests. Other products or
revisions are not implicitly supported. The `r8152` driver name does not broaden
that allow-list; RTL8139 is unrelated.

## First boot and persistent settings

The firmware contains the reviewed module files in `/lib/modules/5.4.61/cc2`
and the complete checked runtime in `/opt/inst/cc2-connections`. Before native
printer/display/camera processes start, the boot initializer seeds the regular
files expected by the existing manager on mounted `/opt/usr`.

Known native and managed DHCP entry points are accepted. A prior component is
retained in a timestamped backup; confirmed network settings are preserved.
Unknown edits, symlinks, running services, damaged inputs and failed commits are
refused, with rollback of the component and DHCP file. The bootstrap issues no
module-load, heater, motion, network-configuration or service-restart command.
The existing manager owns subsequent connection changes and recovery.

The original printer startup script and service order remain unchanged. The
selected A/B OTA branch sets the existing `ota_flag` so the native image/font
synchronization runs before the GUI. New display resources are installed in
`/opt/inst/images/theme_default`; no replacement GUI startup wrapper is used.
Personal configuration, mesh, PID and calibration files are not replaced.

## Local build

Required inputs remain local and ignored by Git:

- `original_firmware/cc2_eeb001_02.01.01.07_20260922210235.zip.sig`;
- the existing qualified keys, OpenSSH component and SquashFS 4.6.1 tools;
- the exact Connections runtime identified in
  `../../experimental/network-connections/runtime.json`;
- WSL Ubuntu with the qualified GCC 6.5.0/glibc 2.23 ARM toolchain described in
  `../../docs/BUILD.md`.

From PowerShell, supply the reviewed local archive:

```powershell
.\Build-Release.cmd -Mode community -ConnectionsRuntime 'C:\path\connections-runtime.tar.gz'
```

The launcher finds the previous builder or accepts `-ExistingBuilder`. It copies
only missing restricted inputs and never prints signing material. `stock` mode
requires the exact qualified stock signing input; `community` mode requires the
matching community key. No input, checksum, ABI or signature refusal is bypassed.

The pipeline runs builder/launcher tests, validates and prepares the current
application and camera/reactor components, repeats tests after preparation,
checks keys and official OTA provenance, then builds and re-extracts the new
RootFS for independent audits before signing the OTA chain. Logs and uniquely
named outputs remain under `logs` and `output`.

## Hardware acceptance

Fixture and filesystem checks do not qualify a complete firmware on a printer.
Test first boot and a second reboot, native display and Canvas, Wi-Fi without a
USB adapter, enabled/disabled Ethernet support, qualified LAN switching and
recovery, local/remote streaming, settings/calibration retention and a print.

During Ethernet activation the Canvas may briefly flash red while native
services realign. Wait for its normal connected state before printing. The
complete firmware installation requires a full printer reboot; application
updates that request a reboot must also be completed before printing.

Credit: **Neme77** for integration and printer testing; **tharje** for camera
investigation; **DamiBFryta** for sustained beta testing; and the original feature
contributors documented in `../../docs/CONTRIBUTORS.md`. Retain all licensing and
third-party notices. Vendor executables, original artwork and signing inputs
must not be committed to this repository.
