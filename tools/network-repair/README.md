# Connections compatibility and manual DHCP repair

The application accepts the exact `.13` manager shipped in Community 4.4.4 and
Universal 07 in addition to its previously accepted managers. The HTTP bridge
continues checking file identity, ownership, permissions and printer readiness.
An Ethernet adapter is **not required** to configure Wi-Fi addressing.

The separate `repair-dhcp.sh` handles one verified startup inconsistency: the
published manager and manifest are intact, but the persistent Wi-Fi startup
delegate was replaced by the recognised native DHCP script. It does not repair
arbitrary networking problems, change saved IP settings, enable Ethernet,
restart services or reboot. No repair runs automatically at application startup.

Run the check first, on a cold idle printer:

```sh
sh repair-dhcp.sh --check
```

Review its audit and output before running `sh repair-dhcp.sh --apply` manually.
An already aligned supported installation returns `DHCP_ALREADY_ALIGNED_NO_CHANGES`
without creating an audit or changing any file. Unknown components, altered
startup files, symbolic links and unavailable/busy manager status are refused.
For the recognised case, the check only creates a private audit directory;
the apply step retains `dhcp.original` there, atomically replaces the one DHCP
delegate, verifies the complete startup manifest and verifies the inventoried
RFID file set and hashes. Configuration and calibration files are not targets.
If post-verification fails, the script attempts to restore its verified backup,
provided the active file still matches the replacement it wrote. It refuses to
overwrite an intervening external change. Retain the audit in either case.

Restoring the delegate does **not** by itself correct an older panel's manager
allowlist. Use an application build containing the compatibility correction
before testing network settings. Neither step completes physical validation
of Wi-Fi addressing or USB Ethernet on its own.

Offline checks:

```sh
python3 cc2-control/tests/test_connections_helper_pins.py
python3 tools/network-repair/test_repair.py --fixture /path/to/reviewed/cc2-connections
```

The latter uses BusyBox and the pinned published component input, without
executing its ARM manager. Tests operate on temporary filesystem copies and
synthetic RFID/configuration/calibration sentinels, including a Wi-Fi-only
case with no USB adapter. They check no-op behavior, read-only admission,
backup, idempotence, refusal of changed inputs and post-check rollback.
The component fixture is an external release input and is not committed here.

Implementation and reported-printer diagnosis: **Neme77**. Related report:
[#440](https://github.com/Neme77/centauri-carbon-2-community/issues/440);
its specific cause remains pending the reporter's diagnostics.
