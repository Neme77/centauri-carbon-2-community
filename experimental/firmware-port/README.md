# Community 4.4 port — official OTA 02.01.01.07 only

**Compatibility requirement:** these patch addresses, ELF bindings and SHA-256 pins apply only to official CC2 OTA `02.01.01.07` (`cc2_eeb001_02.01.01.07_20260922210235.zip.sig`). They cannot be reused with the older firmware base. Unknown input hashes remain rejected.

The maintainer built and signed the combined Control/Canvas-reset R2 firmware in WSL and reported a successful initial printer trial after resetting CC2 Control. The earlier 1.2.3 operational fixes received positive beta reports including 30 hours of uninterrupted printing. These reports do not qualify another firmware base or replace continued printer validation. Polish/Czech and the source-only payload generation step are new in this PR.

The generated ARM wrapper is not committed. `builder/current/prepare_reactor.py` reproduces it from `dual_verify_44.S` and the public key with the qualified GNU ARM tools; the existing 792-byte size and SHA-256 must match before it is written or used. Standalone reproduction: `python3 experimental/firmware-port/assemble_dual44_gnu.py --prefix arm-linux-gnueabihf` (binutils only). The full reactor continues to require GCC 6.5.0/glibc 2.23 and the exact qualified vendor inputs.

The sections below retain the original port audit and its limitations.

# OTA 02.01.01.07 -> community 4.4: experimental port

This directory contains the reviewed binary patch manifests and sources used by
the hardware-test candidate builder. It is not a firmware image.
The integration branch is based on develop commit 5740954, including merged PRs
100 and 101. builder/current now targets this OTA on the isolated port branch;
no changes have been published to develop.

Implemented:
- offline SHA256-pinned Dual Trust patch for the new OTA daemon;
- ARM wrapper emulation: stock success, community fallback, rejected signature,
  allocation/init/control/lock/unlock failures, 1000 repeated calls;
- offline display Z menu patch at VA 0x752d0 -> existing menu path 0x756d8;
- draft reactor bindings/fingerprints for the new vendor executable;
- native bridge unit tests and ASan/UBSan (LeakSanitizer unavailable in sandbox);
- ARM O0 qualification on the printer: all bridge/native-libco cases passed;
- draft SHA256-pinned HTTP upload/cancel port with relocated branch targets;
- 128 ARM-emulated HTTP state-guard cases, including an intentionally different
  value at the obsolete member offset to detect accidental old-layout reads;
- reconstruction and relocation of eight legacy MQTT coexistence hooks;
- SHA256-pinned draft LAN/WAN/webcam patch and 40 ARM-emulated startup/teardown
  helper cases. Vendor calls are mocked and this is not a network test.

Important layout change: the new daemon has only eight bytes before the next
PT_LOAD, insufficient for the 792-byte wrapper. The draft adds an aligned RX
PT_LOAD by reusing PT_NOTE and appending the wrapper; the ABI note section
remains in the existing RX mapping. It extends RW zero-fill by 76 bytes for the
mutex/key cache. This ELF loader arrangement requires validation on ARM before
it can be accepted for a release. No vendor code cave is overwritten.

The callback JSON ABI remains 3.12.0 in both compared binaries; libcolib.so has
identical SHA256. Addresses have changed. Only callbacks and timer code compare
identically after normalizing calls; poll/wait differences inspected so far
are relocated logger strings. Vendor binding and layout audits remain a separate
condition from mocked unit-test success.

HTTP upload/cancel functions have changed length by eight bytes; old numeric
patch offsets cannot be copied. The first candidate audit is retained as history;
http44-manifest.json pins the relocated draft instructions. DeviceStatus machine
status moved from +0xbb4 to +0xbbc, confirmed in the vendor JSON getter. The
reclaimed overwrite block also has changed logger line-number immediates.
Emulation intercepts vendor setters/getters and does not exercise HTTP networking.
Real LAN/WAN MQTT coexistence, streaming, loader checks and new-vendor hardware
integration remain outstanding. The new printer has enough verified zero-filled
RX tail for the 292-byte helper; its existing RX program header is extended
without moving the next load segment. Dual Trust uses a different ELF strategy
because the OTA daemon does not have that space. The full ARM build and OTA signature generation run on the owner's qualified WSL
toolchain. Hardware integration remains unvalidated.

Commands for offline patches:
python3 patch_dual44.py INPUT_DAEMON NEW_OUTPUT
python3 test_dual44_emulation.py NEW_OUTPUT
python3 patch_zoffset44.py INPUT_GUI NEW_OUTPUT

Dependencies for ARM emulation: unicorn. No private keys are needed.
The scripts do not install anything and refuse unknown input hashes.
