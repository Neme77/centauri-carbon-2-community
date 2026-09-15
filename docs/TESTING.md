# Firmware v3.8 test record

## Real-printer validation

The maintainer reported successful tests of the final v3.8 build on a Centauri Carbon 2:

- normal boot and printing functions;
- Matrix/WAN service operational;
- local OrcaSlicer connection while WAN mode is enabled;
- live nozzle and bed temperatures without manual refresh;
- local file upload in WAN mode;
- local webcam in WAN mode;
- local webcam and Matrix video at the same time;
- WAN -> LAN -> WAN switching;
- local port 8080 available in WAN mode;
- stock restoration on both update partitions followed by successful reinstallation of v3.8.

## Builder validation

The v3.8 source preparation was checked for:

- exact printer reference size and hashes;
- rejection of corrupted, truncated, extended, or unknown references;
- inversion of the webcam-only patch back to the expected MQTT v2 hash;
- preservation of file permissions;
- unchanged v3.7 public keys, SSH configuration, and Dual Trust inputs;
- stock and community launcher paths;
- paths containing spaces;
- build option and error propagation;
- Python syntax and automated unit tests.

## Known limits

- Compatibility outside Centauri Carbon 2 firmware 02.01.00.00 is not established.
- The video-client limit remains four.
- ELEGOO Slicer may block busy-printer uploads on the client side.
- Upload during printing does not create a print queue.
- The public source repository cannot reproduce the final MQTT printer binary without the separately supplied pinned reference.

