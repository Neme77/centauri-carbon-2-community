# Port 02.01.01.07 → 02.01.01.13: Overview and reproduction

Local work only. Nothing committed to a shared branch beyond this
`experimental/firmware-port-0113/` directory, nothing flashed, no device
access. This is the top-level entry point for the handoff package; see
`HANDOFF_PACKAGE_CHECKLIST.md` for what to actually send.

**⚠️ Read `OPEN_QUESTIONS.md` before using any binary from this package.**
Two superseded builds exist, each fixed by the next:
- `elegoo_printer.k1` (SHA-256 `adb8529358f12c7f5f10dcfdd8a9e30a0c385cb6bfd8
  b404d8bcb42b5d6abcc6`, referenced in the original GitHub handoff issue):
  payload body mostly zeroed (point 0).
- `WORK/corrected/elegoo_printer.patched` (SHA-256 `2828a99a42993788bbe3967
  5909d832ed67fe1f2ab432d1ae305bad2524f8f4a`, referenced in PR #336): fixed
  the zeroed body, but still had 6 unrelocated branches + 2 stale string
  pointers inside the payload — this is what a real-hardware test crashed on
  (SIGSEGV, see the 2026-11-08 entry in `OPEN_QUESTIONS.md`).

**Use `WORK/corrected_v2/elegoo_printer.patched`** (SHA-256 below) — the
current, fully re-verified build.

## What was ported

The existing `experimental/firmware-port/` community patches (HTTP upload
guards, LAN/WAN network coexistence, GUI Z-offset, camera pause/resume
coexistence) from ELEGOO base **02.01.01.07** onto the new base
**02.01.01.13**. Functions shift mostly `+0x7790` (per-function, never
global); a few individual shifts differ (`+0x2530`, `-0xd8`, `-0xe4` — rebuilt
PLT stubs). See `PORTING_METHODOLOGY.md` for the general technique and
`README.md` for the full per-site analysis.

## Script order (stock inputs → patched binaries)

```
                      stock elegoo_printer ──┐
network44-manifest.json "after" (0xcd77e8) ──┤
                                              ▼
                                    apply_patches.py
                                              │
                      stock ec-eeb001-gui ────┤  (single-word patches, same script)
                      stock ai_camera ────────┘
                                              ▼
                      OUT_DIR/elegoo_printer.patched
                      OUT_DIR/ec-eeb001-gui.patched
                      OUT_DIR/ai_camera.patched
```

Internally `apply_patches.py` runs these stages (each individually documented
and, before this script existed, previously run as separate manual steps —
see `PATCH_SITES.md` for what each stage writes):

1. `k1_restructure.py` logic — ELF restructure (shifts `LOAD[4]`/`DYNAMIC`/
   `PT_TLS` + section table by `+0x10000`, extends `LOAD[3]`) and payload
   placement at VA `0xcc4fac` / file `0xcb4fac`. Behavior-neutral by itself;
   verified by `stage0_verify.py` (82 checks).
2. The 2 verbatim block-inserts (112 B `set_mode`, 444 B `handle_file_upload`),
   read directly from `experimental/firmware-port/http44-manifest.json`.
3. The 10 verbatim data/NOP/in-function patches, read directly from the
   http44/network44 manifests.
4. The 12 payload-reference branches + 2 cross-function branches, computed
   deterministically (ARM A32 `b`/`bl` encoding, see `PORTING_METHODOLOGY.md`).
5. `verify_patched.py` — checks all 42 branch/placement sites; aborts the
   write if any check fails.
6. The GUI and camera single-word patches (with a before-byte assertion each).

### Expected inputs/outputs per script

| Script | Reads | Writes | Read-only? |
|---|---|---|---|
| `apply_patches.py` | 3 stock binaries + 2 manifest JSON files | 3 patched binaries + optional JSON report | No — writes the 3 outputs |
| `k1_restructure.py` | 1 stock `elegoo_printer`, optional 500 B payload | 1 restructured binary + optional JSON report | No — writes 1 output |
| `stage0_verify.py` | 2 ELF files (old, new), optional payload | stdout only | **Yes** |
| `verify_patched.py` | 1 patched `elegoo_printer` | stdout + optional JSON report | **Yes** |
| `emulate_e1.py` | 1 patched `ec-eeb001-gui` | stdout | **Yes** (loads into Unicorn, no file writes) |
| `emulate_e2.py` | 1 patched `ai_camera` | stdout | **Yes** |
| `emulate_e345.py` | 1 patched `elegoo_printer` | stdout | **Yes** |
| `emulate_e6.py` | 1 patched `elegoo_printer` | stdout | **Yes** |
| `scan_payload_branches.py` | 2 stock `elegoo_printer` (old, new) + manifest | stdout + optional JSON report | **Yes** |
| `emulate_crash_20261108.py` | 1 patched `elegoo_printer` | stdout | **Yes** |

## SHA-256 checklist

**Stock inputs (unmodified vendor files, from the official 02.01.01.13 base):**

| File | SHA-256 |
|---|---|
| `elegoo_printer` (stock) | `f0111e0b52c2ea39ff2aae70675c53b311f7340df8ed233e16e8e0cc9f883e14` |
| `ec-eeb001-gui` (stock) | `8d6cac2aecb26b0e504b9d1b71a8b5bb9168fc14143eb0e8fcfc5d7d8f2507fc` |
| `ai_camera` (stock) | `e8df8e57fdc1866b4d7220c3abca08ebc16ca71d759cb7721afafc572f47365f` |
| ELEGOO 02.01.01.13 inner `.swu.sig` | `06fd9fc7…` (matches `builder/current/base_profiles/02.01.01.13.json` `stock_inner`) |

**Current patched outputs — USE THESE (reproducible via `apply_patches.py`,
`WORK/corrected_v2/`):**

| File | SHA-256 |
|---|---|
| `elegoo_printer.patched` | `688b7bf8a27762d56b35d9738ea5b0c3d45580b402e122b0f77ee49dab1a8cfd` |
| `ec-eeb001-gui.patched` | `668ab8cb490c74eb5bbd769b421f6e4ade4e91fba322e22257496b9645f9ebfe` |
| `ai_camera.patched` | `90da7634c21b74b3bf943d68129da78668b47f9706620e158283697ab95553a7` |

**Superseded — do not use (see `OPEN_QUESTIONS.md`):**

| File | SHA-256 | Defect |
|---|---|---|
| `elegoo_printer.k1` (earlier manual build, referenced in GitHub issue #334) | `adb8529358f12c7f5f10dcfdd8a9e30a0c385cb6bfd8b404d8bcb42b5d6abcc6` | payload body ~70% zeroed (point 0) |
| `WORK/corrected/elegoo_printer.patched` (referenced in PR #336) | `2828a99a42993788bbe39675909d832ed67fe1f2ab432d1ae305bad2524f8f4a` | 6 unrelocated branches + 2 stale string pointers in the payload; **caused a real-hardware SIGSEGV** (2026-11-08 entry) |

GUI and camera hashes are identical across all three printer-fix iterations
— only `elegoo_printer`'s payload body was ever affected.

## Comparing against another independent port

`compare_patched_binaries.py` (read-only) diffs two same-architecture ELF
binaries function-by-function via their symbol tables — e.g. your own patched
`elegoo_printer` against ours, or stock against patched to see what changed.
It reports, per named function, whether it's byte-identical, same-size-but-
different (both sides patched it — worth comparing), or size-differs (likely
recompiled/restructured differently), without needing to know either side's
patch offsets in advance.

```sh
python3 compare_patched_binaries.py YOUR_PATCHED_elegoo_printer WORK/corrected_v2/elegoo_printer.patched --only-diffs
```

Self-test (stock 02.01.01.13 vs. this port's corrected output) correctly and
automatically finds exactly our 12 patched functions and nothing else — see
`COMPARE_TOOL_SELFTEST_OUTPUT.txt`. One caveat printed by the tool itself:
its function-size estimate (next symbol's address minus this one's) can
overestimate near sparse/trailing symbols (e.g. `_fini`); ignore size/byte
diffs reported there.

## Status

- `ported_patch_set` in `builder/current/base_profiles/02.01.01.13.json` stays
  **`false`**. The builder is fail-closed for this base by design; this
  package does not change that, and nothing here is a request to change it
  without a passing hardware test.
- Static verification: `verify_patched.py` → **42 PASS, 0 FAIL**, plus a full
  payload branch/address sweep with **zero unresolved sites** (25 branches +
  2 string pointers, all resolving to named vendor functions or staying
  correctly intra-payload) — on `WORK/corrected_v2/elegoo_printer.patched`.
- Emulation (Unicorn, vendor functions intercepted): **4/4 existing harnesses
  PASS** (E1 GUI, E2 camera, E3/E4/E5 HTTP guards — 128 PASS, E6
  LanStrategy-NOP) **plus** a new targeted regression test for the
  2026-11-08 crash (`emulate_crash_20261108.py`) — PASS on `corrected_v2`,
  FAIL (reproducing the exact crash target) on the superseded `corrected`
  build. See `VERIFY_AND_EMULATE.md` and `OPEN_QUESTIONS.md`.
- Hardware test: **one crash found and fixed (2026-11-08); not yet re-tested
  on hardware.** See `OPEN_QUESTIONS.md` and `HARDWARE_TEST_*.md`/
  `ROLLBACK_PLAN.md` for the plan.
