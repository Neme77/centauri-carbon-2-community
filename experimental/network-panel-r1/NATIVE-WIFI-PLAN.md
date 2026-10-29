# Native Wi-Fi integration decision

User direction: use the original ELEGOO display approach for Wi-Fi. CC2 is a frontend to the native manager, not a second independent Wi-Fi manager.

## Confirmed evidence

- The existing wpa_supplicant control endpoint is /opt/usr/wifi/sockets/wlan0. STATUS and SIGNAL_POLL were read successfully on hardware.
- The qualified wifi_daemon references libwifimg.so, aw_wifi_on, aw_wifi_get_wifi_state, aw_wifi_get_wifi_event, start_udhcpc, and a /var/run/wifidaemon socket. Its strings identify scan, list network and disconnect operations. Strings alone do not establish the wire protocol or the display call path.
- The original DHCP wrapper kills all udhcpc processes. The original lease script can replace default routes and DNS. Directly starting a second DHCP service is therefore not an acceptable integration.

## Implementation boundary

- Verify the native display/client call path and request format before exposing mutations.
- Use native scan, connection, saved-profile and radio operations where qualified. Do not read passwords into status responses, duplicate network credentials, or introduce an independent Wi-Fi configuration file.
- Keep Ethernet an optional extension. Coordinate its DHCP and routing with the native hooks; the previous successful temporary Ethernet/cloud trial does not qualify persistent configuration.
- Preserve independently verified recovery before changing the connection carrying the browser session.
- Return to the canonical application compiler. Do not introduce a new libc/toolchain alongside network integration.

## Recovery and release gate

R1 is withdrawn. Installer pin validation now rejects a mismatched candidate. The recovered executable matches the backup; MemAvailable returned to 32904 kB after removing recognized trial files. The supplied kernel tail shows no OOM/oops/panic, but does not identify the collector stall. A replacement installer must not be offered solely on the basis of a corrected hash or UI test. First qualify the bounded status reader separately and investigate the resource regression without restarting printer/camera services.
