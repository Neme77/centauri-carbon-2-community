# Remediation report — Appearance customization

This report records the remediation of the independent review in
[`FINAL_REVIEW_REPORT.md`](FINAL_REVIEW_REPORT.md). It is written by the
remediation engineer and is not an independent verification; the final
adversarial review is recorded in §10.

Scope of the remediation: every finding H-01…H-06 and M-01…M-11 of the review,
plus defects found while integrating and testing (listed in §8). No physical
printer was available; hardware-only risks are listed in §9.

---

## 1. H-01 — Appearance document capacity and persistence

**Original finding.** A document at the advertised capacity (four profiles, or
four layouts with eight themes) was accepted with `{"saved":true}` and could not
be parsed at the next start: `recovered:true`, "not valid JSON", defaults
served, profiles/themes/layouts lost. The shared JSON decoder allows 640 tokens;
the advertised maximum needs about 3 600.

**Root cause.** The appearance module reuses the decoder budget designed for
request bodies and MQTT snapshots. Serialization and parsing had no common
capacity model, and no test round-tripped the advertised maximum through disk
and a process restart.

**Change.**

- `src/connections/json.h` is now a thin wrapper around a parameterised body
  (`src/connections/json_body.h`). The default stays 640 tokens / 16 KiB
  strings / 32 KiB document for every existing consumer, unchanged.
- `src/connections/json_large.h` includes the same body with 6 144 tokens,
  32 KiB strings and a 64 KiB document bound, renaming every symbol to
  `ngl_*` so both decoders can exist in `main.c`. The struct is ~230 KiB and
  every caller heap-allocates it (no stack use).
- `src/appearance.h` uses the large decoder and:
  - parses every saved document back (`ap_verify_serialized`) before writing;
    a save that cannot be read is refused with 500 instead of acknowledged;
  - fsyncs the containing directory after the rename (`ap_sync_parent`);
  - refuses an existing appearance file that is not a regular file or is
    outside the size bound, and reports "invalid JSON or above the document
    limits" instead of blaming corruption;
  - exposes `unsupported` for a newer schema and clears `recovered` /
    `unsupported` / `error` after every successful mutation;
  - caps custom theme ids at 24 characters (`AP_THEME_ID_MAX`), the same shape
    the legacy `/api/preferences` field accepts.
- Themes in a document `PUT` are merged by id instead of replacing the library,
  and `POST /api/appearance/themes/delete` removes one theme explicitly.

**Files.** `cc2-control/src/connections/json.h`,
`cc2-control/src/connections/json_body.h` (new),
`cc2-control/src/connections/json_large.h` (new),
`cc2-control/src/appearance.h`, `cc2-control/src/main.c`,
`cc2-control/tests/test_appearance_api.py`,
`cc2-control/tests/test_appearance_capacity.py` (new),
`cc2-control/tests/test_theme_contract_static.py` (new).

**Regression test.** `tests/test_appearance_capacity.py` builds the maximum
through the HTTP API (six profiles, each with four layouts of nine widgets, and
eight themes with 64-character names and every field at its bound), saves it,
restarts the process, and compares profiles, layouts, widgets, spans, accents,
themes, colours, touchscreen values and the active profile. It also deletes a
theme and restarts again.

**Result.** The maximum document is 17 850 bytes and survives the restart with
every value intact. The test is reproducible with
`python3 tests/test_appearance_capacity.py build/cc2-control`.

---

## 2. H-02 — Display manifest parsing

**Original finding.** The builder generates a 154-asset, 21 848-byte,
1 703-token manifest; the 640-token parser rejected it, so every firmware
built with the feature reported `supported:false` / `display_manifest_invalid`.

**Root cause.** Same as H-01: one parser budget shared with small request
documents, and tests that only used one or two synthetic assets.

**Change.** The display manifest, baseline manifest, journal and state use the
large bounded decoder (`ngl_*`). The manifest schema is still exactly 1; asset
names, roles, dimensions, digests, duplicates and the asset count are
validated as before; the asset-count bound stays 320 and the reader bound is
64 KiB for metadata.

**Files.** `cc2-control/src/display_theme.h`,
`cc2-control/tests/test_display_capacity.py` (new).

**Regression test.** `tests/test_display_capacity.py` imports the production
builder module, generates the real manifest with a synthetic GUI hash, writes
the 154 asset files, starts the daemon and asserts `supported:true`, 154
assets, the role split (100 boot, 4 background, 50 accent), 532×300 boot
dimensions and no error.

**Result.** PASS: the real manifest is parsed and reported supported.

---

## 3. H-03 — Baseline/state capacity and the daemon crash

**Original finding.** The baseline with ~154 entries exceeded the parser; the
state was written in an 8 KiB buffer and read in a 4 KiB buffer; an unreadable
`state.json` caused a SIGSEGV via an unchecked `fopen`.

**Root cause.** Fixed-size buffers and an unchecked file open in
`disp_state_read`, plus the shared parser limit.

**Change.**

- One metadata bound (`DISP_META_MAX`, 64 KiB) is used by the state, journal
  and baseline writers **and** readers; the state is read through a helper that
  checks `lstat`, the regular-file type, the size, `open`, short reads and EINTR
  (`disp_read_json_file`). A missing file is "no state"; a bad one sets
  `stateValid:false` and never crashes.
- The state write buffer is 64 KiB (was 8 KiB), so a 154-entry state fits.
- The JSON parser is the large variant (H-02).
- An unreadable state no longer blocks a restore (the baseline still verifies
  the target) and an apply starts a fresh tracking set while the baseline keeps
  every original.

**Files.** `cc2-control/src/display_theme.h`,
`cc2-control/tests/test_display_theme.c`,
`cc2-control/tests/test_display_capacity.py`.

**Regression tests.**

- `test_display_capacity.py` presents a missing, mode-000, corrupt and 200 KiB
  state file to a fresh process, then a valid 154-entry state; each request
  answers 200, `stateValid` is correct, and the process never exits.
- `test_display_theme.c` writes and reads a 154-entry state (phase and last
  name compared), builds a 154-file baseline, customises every file and
  restores all 154.

**Result.** PASS in both tests; the SIGSEGV is not reproducible.

---

## 4. H-04 — Restore independent of the current manifest; `manage.sh`

**Original finding.** Restore and startup recovery required the current
manifest and GUI hash, so after an update the baseline was unreachable, and
`experimental/native-display/manage.sh` refused the studio's files as foreign.

**Root cause.** Capability for *applying* a pack was also used as the
authorisation for *restoring* a baseline, and the baseline did not record the
theme directory it belongs to.

**Change.**

- `disp_baseline_read` records and checks `themeDir` and can be called without
  comparing the current manifest (`compare_current = 0`).
- `disp_restore_response` and `disp_startup_recover` no longer require
  `disp_capability()`. They verify every backup against its recorded SHA-256,
  copy only over a file that is the original or a version this feature recorded
  (state, journal), and report `firmwareChanged` when the saved identity
  differs from the current one.
- A **new** apply is refused (`display_firmware_changed`) while a baseline from
  another firmware/GUI exists, so a new identity can never be written over
  another version's originals.
- A successful restore removes a leftover journal and syncs the directory.
- The studio shows a separate restore card whenever `restoreAvailable`, with a
  warning when `firmwareChanged`, even if the manifest is currently
  unsupported (recovery is not gated on the experimental switch).
- Documentation (`docs/APPEARANCE.md`, `cc2-control/docs/API.md`) explains the
  interaction with `manage.sh`: restore in the studio first, then `manage.sh`
  sees the vendor originals again; the two tools must not run concurrently.

**Files.** `cc2-control/src/display_theme.h`,
`cc2-control/tests/test_display_theme.c`,
`cc2-control/web-src/src/lib/display.ts`,
`cc2-control/web-src/src/pages/appearance.tsx`,
`docs/APPEARANCE.md`, `cc2-control/docs/API.md`.

**Regression test.** `test_display_theme.c` step 5 changes the GUI file and the
manifest, asserts that a new apply is refused with `firmware_changed`, and then
asserts that `disp_restore_baseline` restores the exact original bytes anyway.

**Result.** PASS. `manage.sh` integration remains documented and
hardware-verification-only (the script is not modified, so its pins stay
valid).

---

## 5. H-05 — Boot artwork and pack size

**Original finding.** A normal 1200×800 photo produced a 53.8 KB boot frame; a
100-frame pack was 5.1 MiB against the 4 MiB server limit, and the client
accepted it.

**Root cause.** The client had an absolute 120 KiB per-frame check and no
knowledge of the total request budget.

**Change.** `web-src/src/lib/display.ts` exports the server limits
(`MAX_PACK_BYTES`, `MAX_FILE_BYTES`, `MAX_ASSETS`, `BOOT_FRAMES`) and:

- `bootFrame` takes a byte budget and quantises (32→16→8→4 levels) until the
  frame fits it;
- `buildPack` builds the non-boot files first, derives the per-frame budget from
  the remaining pack space, checks every file against 512 KiB, and refuses the
  whole pack before upload if it cannot fit;
- `buildTar` refuses an oversized tar.
- `tests/test_display_limits_static.py` compares the client constants with
  `DISP_MAX_PACK` / `DISP_MAX_FILE` / `DISP_MAX_ASSETS`, so they cannot drift.

**Files.** `cc2-control/web-src/src/lib/display.ts`,
`cc2-control/web-src/preview/test-display-pack.mjs` (new),
`cc2-control/web-src/package.json`, `.github/workflows/ui-preview.yml`,
`cc2-control/tests/test_display_limits_static.py` (new).

**Regression test.** `preview/test-display-pack.mjs` builds the same
photo-like 1200×800 JPEG, runs the real `bootFrame` and `buildPack` for 100
frames, and asserts the result is inside 4 MiB; it also asserts that an
oversized asset and an oversized tar are refused before upload.

**Result.** PASS: the photo now produces a **3.42 MiB** pack (previously
5.1 MiB / rejected).

---

## 6. H-06 — Upstream integration

See §7. The branch is merged with the current `origin/develop`, the four
generated/pinned files are regenerated from their sources, and the source
snapshot is committed last.

---

## 7. Additional review findings

| Review ID | Status | Change |
| --- | --- | --- |
| M-01 crash on unreadable state | Fixed | `disp_read_json_file` checks open/type/size/short read; tested with mode 000, corrupt and oversized files |
| M-02 HTTP blocked during upload | Fixed | The upload thread no longer holds `disp_mutex` while streaming; a state snapshot is taken under a short lock and published at the end; `test_display_theme.c` asserts a status answer during an upload |
| M-03 unverified/partial backups | Fixed | Backups are written atomically and verified against the recorded digest; restore verifies all backups in a first pass before copying anything |
| M-04 imported theme tokens | Fixed | `createTheme`/`updateTheme`/the theme-file import run `normalizeTokens`; an invalid theme is refused with a translated message |
| M-05 partial import | Fixed | The import refuses up front when the printer cannot hold the imported profiles or themes, and writes themes before the dashboard that names them |
| M-06 concurrent theme writes | Fixed | The document `PUT` no longer carries the theme library; themes use `POST /themes`, `POST /themes/delete` and merge-by-id semantics |
| M-07 theme id contract | Fixed | One 24-character `[a-z-]` rule in the studio, the importers, `/api/appearance` and `/api/preferences`; a static test compares all three |
| M-08 Advanced tab | Fixed | `advanced: Wand2` added to the icon map; every tab is opened by the browser test |
| M-09 hardware/manage.sh | Documented | See §4 and the hardware checklist in `docs/APPEARANCE.md`; not hardware-tested |
| M-10 size headroom | Improved | Shipped locale catalogues are compact JSON (ru.json 243 130 → 234 088 bytes, below its pre-remediation 241 328); the document is 943 009 bytes, 19 551 below the alarm |
| M-11 journal durability | Improved | Directory sync after rename/unlink; journal replayed idempotently; a successful apply's journal removal is synced |
| L-01 `DBG` prints | Fixed | Removed |
| L-02 page title | Fixed | The studio uses the shared `<Page>` component |
| L-03 duplicate accessible names | Fixed | Each hex field names its token |
| L-04 reboot notice | Fixed | The recorded boot id is compared as well as the GUI marker |
| L-05 directory sync | Fixed | `ap_sync_parent` after every rename/unlink of metadata and backups |
| L-06 diagnostics | Fixed | "invalid JSON or above the document limits"; `unsupported` exposed; older bytes kept in `.bak` |
| L-07 changelog | Fixed | Fixed/Changed entries added; the PR number is still to be added when the pull request is opened |
| L-08 client timeout | Fixed | The upload timeout is 360 s against the server's 300 s transfer window |
| L-09 staged leftovers | Fixed | A refused/rolled-back pack removes its staged files |
| L-10 global request limit | Accepted | The 16 KiB buffer is heap-allocated and bounded; a per-route limit is a future cleanup |
| L-11 documented fallback | Fixed | `docs/APPEARANCE.md` now describes the per-section recovery accurately |
| L-12 localStorage writes | Accepted | A performance note, not a defect; changes are still debounced for the network |
| L-13 system preview | Fixed | The theme gallery listens to the OS colour-scheme change |

### 7.1 Findings of the independent adversarial review, and their disposition

The reviewer reproduced H-01, H-02, H-03, H-05 and H-06 as fixed, confirmed
H-04 as partially fixed, and found the following items. All were addressed in a
second remediation round, except where noted.

| ID | Sev | Finding | Disposition |
| --- | --- | --- | --- |
| F-01 | MEDIUM | A restore after a firmware/GUI change left the stale baseline in place, so a new apply stayed refused (`display_firmware_changed`); if the vendor also rewrote the theme files, restore refused too, leaving no API/UI path. | **Fixed.** A successful restore now deletes the baseline (the originals are back), so the next apply takes a fresh one. An apply also discards a stale baseline automatically when every target is provably an original (the old baseline or the current firmware's own image). Both paths are covered by `test_display_theme.c` steps 5b/5c. |
| F-02 | LOW | The shortest generated id (`custom-a`, from a one-letter theme name) was accepted by the printer but rejected by the client normaliser, so the theme vanished on reload and occupied a slot. | **Fixed.** Both validators now accept one to 17 characters after `custom-`; `test_appearance_api.py` saves `custom-a` and restarts, and the browser test creates a one-letter theme and reloads. |
| F-03 | LOW | The HTTP transport caps a request body at 16 KiB while `AP_MAX_JSON` advertised 32 KiB; the maximum document could not be sent in one PUT. | **Fixed.** `AP_MAX_REQUEST` (16 KiB) is separate from the stored-document bound (32 KiB); the documentation says a client changes one section at a time. |
| F-04 | LOW | A successful rollback left the journal, so the feature stayed blocked until a restore or restart. | **Fixed.** The journal (and its directory entry) is removed when the rollback succeeded; a test makes the state write fail and verifies the rollback and the absent journal. |
| F-05 | LOW | A declared `Content-Length` shorter than the stream could make the upload thread read past the body and block until the socket timeout. | **Fixed.** The header, body and padding reads are guarded by the remaining declared length; a test sends more bytes than declared and asserts a prompt answer. |
| F-06 | LOW | `local_state` in `disp_receive_pack` was not fully initialised before the failure path copied it into the state. | **Fixed.** `disp_state_defaults()` zeroes it. |
| F-07 | LOW | The client ignored the server's `unsupported` flag, and the local `recovered` banner persisted after a successful save. | **Fixed.** `unsupported` is carried in the document, the banner covers both, and a successful save clears both locally. |
| F-08 | LOW | The theme editor announced "saved" even when the theme was refused. | **Fixed.** `updateTheme` reports success; the editor closes and confirms only then. |
| F-09 | INFO | Size alarms were close to the hard limits (locale 94 %). | **Improved, then re-based on upstream.** Before the final integration the alarm was 230 KiB (ru.json 234 088 B); upstream then added 38 strings, so the alarm is 240 KiB (93.7 %; ru.json 239 922 B) and the page alarm 940 KiB (index 950 405 B). Hard limits unchanged; current figures are in the pre-PR audit report. |
| F-10 | INFO | The changelog entries lacked a PR number/credit and understated the maximum document (18 KiB vs 20 746 B measured). | **Partly fixed.** The size wording is corrected; the PR number and the per-entry credit must be added when the pull request is opened (they cannot exist before it). |
| F-11 | INFO | `DISP_MAX_ASSETS` 320 was unreachable because the manifest was capped at 32 KiB, and `firmwareChanged` was false without a manifest. | **Fixed.** The manifest accepts up to 64 KiB (the large decoder bound), so 320 assets fit; `firmwareChanged` is true when a baseline exists and the current manifest is unavailable. |
| F-12 | INFO | An import is not atomic across a network failure. | **Documented** as a known limitation (capacity is checked first; items written before a failure remain). |

A third targeted round closed the two items the reviewer found in the fixes
themselves and the remaining hardening:

| ID | Sev | Finding | Disposition |
| --- | --- | --- | --- |
| N-01 | LOW | A successful apply that discarded a stale baseline reported the `display_firmware_changed` identifier of the refusal that preceded it. | **Fixed.** `disp_clear_error()` runs after a successful discard. |
| N-02 | LOW (test) | The negative half of the stale-baseline guard (customised bytes present) was not in the suite. | **Fixed.** `test_display_theme.c` step 5d asserts the refusal and that the baseline is kept. |
| F-05 residual | INFO | The entry size was not compared with the remaining declared body. | **Fixed.** An entry larger than the remaining body is refused before it is read. |
| F-11 residual | INFO | A 320-asset manifest needs more than the 32 KiB string arena. | **Fixed.** The large decoder's arena is 64 KiB; `test_display_capacity.py` proves both the real 154-asset and a synthetic 320-asset manifest. |

The reviewer's weak-test observations were also addressed: the theme-contract
test and the API/browser tests now exercise the one-letter boundary, the
capacity test uses longer names, and the display harness covers the failed
state write, the mismatched `Content-Length`, the real HTTP restore with
baseline removal and the refused stale discard.

Additional defects found during remediation and fixed:

- `source-variants.json` needs a reviewed entry for the split decoder, or the
  qualified Connections helper test fails. Added with both hashes and a reason.
- `themePreviewStyle` was an unused export; removed.
- The theme editor's delete path called the old synchronous API; it now awaits
  the explicit delete and shows errors.
- A long theme name could exceed the legacy 24-character field and make the
  Settings save fail; the generated id is now truncated inside the contract.
- The Advanced tab's missing icon threw in the development/demo build; fixed
  and covered.
- The document save used to race the theme creation; themes are now saved
  before a dashboard that names them.
- A failed theme request could leave the local document and the printer out of
  step; a failure reloads the stored document.

---

## 8. Tests added

| Test | What it proves |
| --- | --- |
| `tests/test_appearance_capacity.py` | Maximum document save → disk → restart → exact values |
| `tests/test_theme_contract_static.py` | 24-character id rule in C/TS/legacy, one palette list, per-item theme writes |
| `tests/test_display_capacity.py` | Real 154-asset and synthetic 320-asset manifests parse; missing/unreadable/corrupt/oversized/154-entry state |
| `tests/test_display_limits_static.py` | Client and server pack/file/asset limits agree |
| `tests/test_display_theme.c` (extended) | 154-file baseline/state, restore across a firmware change, apply refusal, status during an upload, stage cleanup |
| `tests/test_appearance_api.py` (extended) | Theme merge, explicit delete, over-long/digit theme ids refused |
| `preview/test-display-pack.mjs` | A normal photo stays inside 4 MiB; oversized assets/tar refused |
| `preview/test-appearance.mjs` (rewritten) | Every studio tab, per-item writes, id length, delete, 320/390 px |

---

## 9. Test results

All commands were run in this environment (x86-64, gcc 16.2.1, Python 3.14.10,
Node 26.13.1). Environment limitations are listed separately; no result is
presented as a hardware test.

### Backend and host scripts

- `make clean test CROSS= CC=gcc`: the build is clean (no warnings); the
  driver stops at `tests/test_binding_observation_retry.py`, which needs the
  `dash` shell that this host does not have. This is an environment limitation
  and also fails at the merge base.
- After the upstream merge, every script run individually (the two
  Makefile-excluded payload tests and the C harness driver omitted here):
  **64 passed, 4 failed only for missing `dash`/BusyBox**
  (`test_binding_observation_retry`, `test_canvas_install_selection`,
  `test_managed_connections`, `test_pc_staging`). The same four also fail at
  the merge base on this host.
- `python3 tests/test_c_harnesses.py` with
  `CC2_TEST_CFLAGS="-Wno-error=maybe-uninitialized"` (GCC 16 reports a
  pre-existing false positive in `tests/test_mesh_api.c`, also reproducible at
  the merge base): **1 744 PASS lines, exit 0**, including the extended
  display harness. Before the merge the same run produced 1 736 PASS lines and
  exit 0.
- A review-only end-to-end check (not committed) streamed a 154-file pack through
  the production `disp_apply_start`/upload-thread path over a real socket: 200
  with `{"ok":true}`, all 154 targets replaced, the state and the baseline each
  holding 154 entries, then `disp_restore_response` returned all 154 originals.
- Focused appearance/display tests: `test_appearance_api`,
  `test_appearance_capacity`, `test_appearance_static`,
  `test_theme_contract_static`, `test_display_api`, `test_display_capacity`,
  `test_display_limits_static` — all PASS.
- Builder tests: `python3 -m unittest discover -s builder/current/tests -p
  'test_*.py'` — **Ran 61, 60 pass, 3 skipped, 1 failure**. The failure is
  `test_connections_firmware.test_actual_bootstrap_success_refusal_and_rollback`,
  which requires the `dash` shell and BusyBox that this host lacks (it also
  fails at the merge base). `test_cc2_control_source_snapshot`,
  `test_pinned_inputs` and `test_cc2_control_integration` pass against the
  regenerated snapshot.

### Frontend

- `npm run check` (strict TypeScript + Biome): PASS, 134 files.
- `npm run build`: PASS; the committed `web/index.html` and
  `web/locales/*.json` are reproduced byte-for-byte.
- `npm run test:appearance`, `node preview/test-display-pack.mjs`,
  `node preview/test-dashboard-style.mjs`, `node preview/test-obsidian-layout.mjs`,
  `node preview/test-browser.mjs`: PASS.
- `python3 tests/test_web_sync_static.py`, `test_polish_czech_static.py`,
  `test_locales_static.py`, `test_locale_coverage_static.py`,
  `test_themes_static.py`, `test_web_size_limits_static.py`,
  `test_polling_static.py`: PASS.

### Sizes

- `web/index.html` before the final upstream integration: 943 582 bytes
  (89.99 % of 1 MiB; 18 978 before the test alarm). The integrated tree is
  measured in [`PRE_PR_AUDIT_REPORT.md`](PRE_PR_AUDIT_REPORT.md).
- Tightest locale `ru.json`: 234 088 bytes (89.3 % of 256 KiB; 1 432 bytes
  before the restored 230 KiB alarm). It was 241 328 bytes before remediation and 218 482 bytes
  before the feature; the nine added strings are included and the compact
  encoding recovered the budget.

### 9.1 Integration with upstream

- `git fetch origin`; `origin/develop` was at `b7daa441` (two commits after the
  merge base `a59ae640`). The branch was merged with `git merge --no-ff
  origin/develop` (no rebase, no history rewrite). Merge commit:
  `764f0ea4b8744d2365dae7df742fd678fcfc01a8`.
- Conflicts were exactly the four generated/pinned files. They were resolved by
  keeping the branch version temporarily and regenerating them from their
  sources afterwards; no hand edit of a generated file was kept. `CHANGELOG.md`
  conflicted and was merged by hand, keeping both the appearance and the two
  upstream entries.
- Upstream functionality is preserved and verified present: `src/spoolman.h`
  (the large-spool/streaming/validation changes), `src/connections_http.h` and
  `src/connections/pin.h` (the published .13 manager digest),
  `tests/test_connections_helper_pins.py`, `tools/network-repair/*`,
  `docs/SPOOLMAN.md`, `cc2-control/docs/API.md` and the workflow/test changes.
  `test_connections_helper_pins.py` and `test_connections_sources.py` pass.
- `python3 experimental/pr128-integration/sync_source.py` was run on the merge
  commit: `source.zip` was regenerated (`sha256
  1aae46ccde2685ba2d2c2f7c5cc717e25c98b0278159c9f266b1323d6418aea8`), and the
  pins in `prepare.py`/`firmware_builder.py` point at the commit that carries
  the same `cc2-control` tree as the merge (the parent of the snapshot commit),
  and `BUILDER_SHA256` is updated. The archive was inspected: it contains both the
  upstream `spoolman.h` changes and the new `appearance.h` /
  `display_theme.h` / `json_body.h` / `json_large.h`, and its `web/index.html`
  is byte-identical to the committed bundle.
- `npm run build` after the merge produced no change in `cc2-control/web`, so
  the committed bundle is the build of the merged sources.
- The regenerated snapshot and pins are committed last, after all source,
  test and documentation changes.

---

## 9.2 Post-remediation integration and pre-PR audit

The branch was integrated again with `origin/develop` after three further
upstream commits (gzip web delivery, Spoolman on-demand sync, ELEGOO error-code
texts). The merge resolutions, the raised locale alarm (240 KiB, required by the
38 new upstream strings; the hard limits are unchanged) and the audit findings
are recorded in [`PRE_PR_AUDIT_REPORT.md`](PRE_PR_AUDIT_REPORT.md). The audit
found and fixed one regression introduced by this remediation (a use-after-free
in the two appearance delete handlers), a contract mismatch between the
advertised 320 assets and the 256-entry stage/state cap, and several frontend
name, focus and overflow defects; the audit report lists them with evidence.

## 10. Remaining risks

1. **No physical printer.** Everything about manifest deployment, vendor
   initialisation, PNG decoding by the GUI, boot behaviour, persistence across
   power cycles and `manage.sh` interaction is simulated only. The checklist in
   `docs/APPEARANCE.md` marks every item unverified.
2. **Firmware synchronisation.** The manifest is installed into
   `/opt/inst/images/theme_default`; whether the vendor sync copies the extra
   JSON file to `/opt/usr/images/theme_default`, and whether it re-syncs on
   every boot, must be confirmed on hardware (the firmware reports
   `supported:false` until the file is present, which is fail-closed).
3. **ARM build.** No ARM toolchain is available here; the host build and the
   builder's static checks pass, but the cross-build is performed by the CI
   environment, not by this host.
4. **Size.** The page has about 10 % headroom and the locale alarm about 4.5 %
   on `ru.json`; compact delivery recovered most of what the feature consumed,
   but the next large feature still needs a size decision (compression, split
   catalogue or a deliberate limit increase in all three places).
5. **GCC 16.** `test_mesh_api.c` needs `-Wno-error=maybe-uninitialized` on GCC
   16; the warning is pre-existing at the merge base and is not caused by this
   change.
6. **`REQUEST_MAX`** remains a shared 16 KiB buffer; it is bounded and heap
   allocated, but a per-route limit would be cleaner.

---

## 11. Files changed by the remediation

Backend: `src/connections/json.h`, `src/connections/json_body.h` (new),
`src/connections/json_large.h` (new), `src/appearance.h`,
`src/display_theme.h`, `src/main.c`.
Tests: `tests/test_appearance_api.py` (extended),
`tests/test_appearance_capacity.py` (new),
`tests/test_theme_contract_static.py` (new),
`tests/test_appearance_static.py`, `tests/test_display_theme.c` (extended),
`tests/test_display_capacity.py` (new),
`tests/test_display_limits_static.py` (new),
`tests/test_web_sync_static.py`.
Frontend: `web-src/src/lib/appearance.ts`, `lib/display.ts`, `lib/i18n.ts`,
`pages/appearance.tsx`, `components/icons.tsx`, `vite.config.ts`, `package.json`,
`preview/plugin.ts`, `preview/test-appearance.mjs` (rewritten),
`preview/test-display-pack.mjs` (new), all locale files, and the rebuilt
`web/index.html` and `web/locales/*.json`.
Builder/integration: `experimental/network-connections/source-variants.json`,
the regenerated source snapshot and pins (§9.1), `.github/workflows/ui-preview.yml`.
Documentation: `docs/APPEARANCE.md`, `cc2-control/docs/API.md`, `CHANGELOG.md`,
this report, and the preserved `docs/FINAL_REVIEW_REPORT.md`.
