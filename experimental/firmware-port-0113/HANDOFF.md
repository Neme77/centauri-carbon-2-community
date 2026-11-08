# Handoff: community-patch port 02.01.01.07 → 02.01.01.13 (experimental)

Local work only. Nothing flashed, nothing committed, no device access. This note
summarizes an experimental port of the existing `experimental/firmware-port/`
community patches onto the new ELEGOO base **02.01.01.13**, for a maintainer with
signing keys and a hardware-test setup to take over the build + hardware steps.

**Status: static patch complete and emulation-verified; NOT hardware-tested.**
`ported_patch_set` stays `false`; the builder is fail-closed for this base by
design. Do not treat this as release-ready.

## Base and inputs (verified)

| Item | SHA-256 |
|---|---|
| ELEGOO base 02.01.01.13 inner `.swu.sig` | `06fd9fc7…` (matches `base_profiles/02.01.01.13.json` stock_inner) |
| Stock `elegoo_printer` (02.01.01.13) | `f0111e0b52c2ea39ff2aae70675c53b311f7340df8ed233e16e8e0cc9f883e14` |
| Stock `ec-eeb001-gui` (02.01.01.13) | `8d6cac2a…` |
| Stock `ai_camera` (02.01.01.13) | `e8df8e57…` |

`daemon-000` (Dual Trust) and `libcolib.so` are byte-identical to 02.01.01.07;
`uboot`/`boot0` identical. Those patches carry over unchanged.

## Patched binaries (output)

| Binary | SHA-256 | Patch |
|---|---|---|
| `elegoo_printer` (K1-restructured + patched) | `adb8529358f12c7f5f10dcfdd8a9e30a0c385cb6bfd8b404d8bcb42b5d6abcc6` | HTTP + network/LAN-WAN + coexistence payload |
| `ec-eeb001-gui` | `668ab8cb490c74eb5bbd769b421f6e4ade4e91fba322e22257496b9645f9ebfe` | Z-offset: `b 0x74bd0` at file `0x647C8` (skip `data_get_print_status`) |
| `ai_camera` | `90da7634c21b74b3bf943d68129da78668b47f9706620e158283697ab95553a7` | `LocalRtcMonitor`: `bl resume (0x16a7f8)` at file `0x12287C` (was `pause`) |

## Printer port details

Functions shift mostly `+0x7790` between the two bases (per-function, not global).

- **ELF restructure (K1):** the old port appended a 500-byte coexistence payload
  at the end of the executable segment; in 02.01.01.13 the end gap is only 84 B,
  so a deterministic restructure shifts `LOAD[4]`/`DYNAMIC`/`PT_TLS` + section
  table by `+0x10000` and extends `LOAD[3]`, placing the payload at VA `0xcc4fac`
  (file `0xcb4fac`). Tool: `k1_restructure.py` (behavior-neutral; `stage0_verify.py`
  = 82 structure checks PASS). GNU_STACK-repurpose (K2) documented as fallback only
  (exec-stack risk) — K1 chosen.
- **Payload-internal branches (17):** four distinct shifts — `+0x7790` (9),
  `+0x2530` (`NetworkManager::get_instance`), `-0xd8`/`-0xe4` (rebuilt PLT stubs for
  `strcmp`/`std::string`). Not a uniform shift.
- **External wiring:** 14 branches into the payload / cross-function, plus 2 verbatim
  block-inserts (`set_mode` 112 B, `handle_file_upload` 444 B — relocation-invariant),
  plus 10 verbatim data/NOP/in-function patches.
- **device_status_offset = `0xbbc`:** statically validated (same DeviceStatus
  accessors read `[this,#0xbbc]` in both builds). Not hardware-confirmed.

## Verification (all local, read-only)

- `verify_patched.py WORK/elegoo_printer.k1` → **42 PASS, 0 FAIL** (17 payload-internal
  + 12 payload refs + 2 cross-function + 10 verbatim; payload placed).
- GUI/camera: single-word patches, before/after and size checked; only the target
  word changed.

## Emulation (Unicorn; vendor functions intercepted)

| Case | Result |
|---|---|
| E3/E4/E5 HTTP upload guards + `device_status_offset` | **128 PASS** (32 states × 4 paths) |
| E6 LanStrategy::stop (no HTTP stop; MQTT stop stays) | **PASS** |
| E1 GUI Z-offset reachable regardless of print status | **PASS** |
| E2 camera pause→resume | **PASS** |

## Open / maintainer required

1. **Build + sign** the three patched binaries into a package (needs private keys;
   builder is fail-closed for 02.01.01.13 — flip `ported_patch_set` only after a
   passing hardware test, per `AGENTS.md`/`docs/TESTING.md`).
2. **Hardware test** per `experimental/firmware-port-0113/HARDWARE_TEST_*.md` and
   `ROLLBACK_PLAN.md` (backup + verified reflash path first; the package replaces
   the bootloader — brick risk).
3. **Confirm by disassembly/emulation:** `LanStrategy::start+0x11c` payload branch
   target `0x8267bc` is structurally inferred (byte-identical target instruction,
   relocated neighbours), not fully disassembled.
4. **Not proven by emulation:** real HTTP/network/MQTT/camera integration, UI, boot.

## Files (under `experimental/firmware-port-0113/`)

Analysis + tooling: `README.md`, `DIFF_BLOCKS.md`, `PORTING_METHODOLOGY.md`,
`PAYLOAD_BASE_FIXATION_AND_BRANCH_PLAN.md`, `K1_FEASIBILITY.md`,
`K1_RESTRUCTURE_SPEC.md`, `TEST_MATRIX.md`, `RISK_LIST.md`,
`COMPATIBILITY_AND_ROLLBACK.md`, `EMULATION_PLAN.md`, `EMULATION_RESULTS.md`,
`HARDWARE_TEST_*.md`, `ROLLBACK_PLAN.md`, `EXECUTION_RUNBOOK.md`.
Scripts (read-only verifiers + restructure + emulation harnesses):
`stage0_verify.py`, `verify_patched.py`, `k1_restructure.py`, `emulate_e345.py`,
`emulate_e6.py`, `emulate_e1.py`, `emulate_e2.py`.

Patched binaries and vendor inputs are local only (git-ignored); not attached here.
