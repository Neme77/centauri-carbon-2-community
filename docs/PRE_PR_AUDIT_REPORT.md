# Pre-PR audit report — Appearance customization

Independent pre-PR audit of the remediated appearance/display feature.
Date: 2026-11-27.
This report verifies the implementation, not the remediation report; every
claim below was checked against the code, a command or an independent
specialist's evidence. No hardware was available or touched.

---

## A. Verdict

**READY FOR PR AUTHORIZATION.**

All six original HIGH findings are independently reproduced as fixed. Two
blocking defects found by this audit were fixed and re-verified (a
use-after-free the remediation itself introduced, and the 256/320 stage limit
mismatch). No known critical or high security issue remains. The remaining
items are hardware-only validation, thin size headroom and documentation
precision; they do not block opening a pull request, but the touchscreen part
must not be described as hardware-verified and the hardware checklist remains a
release gate.

---

## B. Repository state

| Item | Value |
| --- | --- |
| Branch | `feature/appearance-customization` |
| Final HEAD | The commit that contains this report and the regenerated snapshot (parent `6b3e6611`; `git log -1`) |
| Code/merge commit | `80033f1d` (merge + audit fixes), then `1b8c0c70`, `e24e7a41`, `a939896f` (docs/test, final fixes, screenshot artifact removal), `76fbdba8` (duplicate-id import), `6b3e6611` (Ember/Rose upstream merge, addendum H) |
| Upstream `origin/develop` | `b5968048` (moved twice during this audit: `3892a174` added gzip web delivery #449, Spoolman on-demand sync #451 and ELEGOO error texts #445; `b5968048` added the Ember/Rose themes #453, see addendum H) |
| Merge base (original) | `a59ae640` |
| Integrated with upstream | Yes: `b5968048` is an ancestor of HEAD; 0 behind |
| Ahead of `origin/develop` | `b5968048..6b3e6611` plus the final snapshot commit (31 commits ahead, 0 behind at publication time) |
| Working tree | clean (`git status --porcelain` empty) |
| Commits added during this audit | `80033f1d` (merge + audit fixes), `1b8c0c70` (docs/test), `e24e7a41` (last fixes), `a939896f` (artifact cleanup), `76fbdba8` (duplicate-id import), `6b3e6611` (second upstream merge), final snapshot commit |
| Pushed / PR | **No push, no PR, no merge into upstream** |
| Merge conflicts | none unresolved; generated web/builder files resolved by regeneration |

The audit used two auxiliary worktrees (`/tmp/audit-335986a6` for parallel
read-only specialist audits, `/tmp/audit-head` for the final sanitizer build);
neither contains tracked changes and both are removable.

---

## C. Findings matrix

### Original HIGH findings

| ID | Severity | Original problem | Status | Evidence | Remaining risk |
| --- | --- | --- | --- | --- | --- |
| H-01 | HIGH | A document at the advertised capacity saved successfully but could not be read back; profiles/themes/layouts were lost on restart | **Fixed** | `tests/test_appearance_capacity.py` PASS (17 850 B, restart, exact values); an independent probe with 6×4×9, 8 themes and 24-char ids stored 20 746 B and compared the whole document after restart; saves are parsed back (`ap_verify_serialized`) | None functional; document is ~21 KiB max |
| H-02 | HIGH | The builder's real 154-asset manifest was rejected by the 640-token parser | **Fixed** | `tests/test_display_capacity.py` PASS: real 154-asset manifest `supported:true`, roles 100/4/50, 532×300; synthetic 320-asset manifest also supported; a 154-file pack applied over the real `disp_apply_start` path (independent probe) | Hardware must confirm the manifest is deployed/copied by the vendor init |
| H-03 | HIGH | Baseline/state exceeded the parser; the state was read into 4 KiB and an unreadable state crashed the daemon (SIGSEGV) | **Fixed** | 154- and 320-entry state/baseline round-trips; fresh processes with mode-000, directory, symlink, corrupt, truncated, 64 KiB and 64 KiB+1 state all answer 200 with `stateValid:false` (or true for the valid boundary) and never exit; ASan/UBSan clean | Power-loss durability is reasoned and host-tested, not hardware-tested |
| H-04 | HIGH | Restore required the current manifest/GUI; after an update the baseline was unreachable and a restore did not re-enable applies | **Fixed** | Restore through the real handler after a GUI/manifest change returns the originals and removes the baseline; the next apply succeeds; a stale baseline is discarded only when every target is provably an original, otherwise the apply is refused and the baseline kept (`test_display_theme.c` 5b/5c/5d); independent probe reproduced all three cases | Vendor may rewrite the theme directory; hardware must confirm the vendor's files match the manifest |
| H-05 | HIGH | A normal photo built a 5.1 MiB pack against the 4 MiB limit | **Fixed** | `preview/test-display-pack.mjs` PASS: the same photo builds **3.42 MiB** for 100 frames; client constants equal the C limits (static test); oversized file/tar refused before upload | Real backgrounds make the non-boot part larger; the derived per-frame budget accounts for it |
| H-06 | HIGH | The branch could not merge and its snapshot omitted upstream work | **Fixed** | `origin/develop` `3892a174` merged; conflicts only in generated web/builder files; rebuilt web from merged sources; snapshot regenerated and committed last; snapshot/pin/integration tests pass; upstream gzip, Spoolman and ELEGOO changes present and tested | Future upstream moves require the same regeneration |

### Remediation-review findings (F/N) and earlier items

| ID | Severity | Problem | Status | Evidence |
| --- | --- | --- | --- | --- |
| F-01 | MEDIUM | Stale baseline dead end after a firmware change | **Fixed** | Restore removes the baseline; stale baseline discarded only when safe; negative case refuses and keeps it (`test_display_theme.c` 5b/5c/5d) |
| F-02 | LOW | One-letter theme id accepted by printer, dropped by client | **Fixed** | Client/server decision matrix identical for 1–17 chars, 24/25, digits, double/edge hyphens; API test saves `custom-a` and restarts; browser test creates and reloads it |
| F-03 | LOW | 16 KiB transport vs 32 KiB document bound | **Fixed / documented** | `AP_MAX_REQUEST 16384` separate from `AP_MAX_JSON`; docs say one section per request; studio payloads measured 3 077 B (dashboard) and 3 573 B (profile) |
| F-04 | LOW | Journal left after a successful rollback | **Fixed** | Journal unlinked + directory-synced when the rollback succeeded; test 7b makes the state write fail and asserts the rollback and absent journal |
| F-05 | LOW | Tar reads could pass the declared body | **Fixed** | Header/body/padding/size guards; 2 ms `display_pack_truncated` answer for a short declared length (independent probe) |
| F-06 | LOW | Uninitialised local state on a failure path | **Fixed** | `disp_state_defaults()` |
| F-07 | LOW | `unsupported` not surfaced; local recovered banner persisted | **Fixed** | Type/normaliser/UI banner; successful save clears `recovered`/`unsupported`/`error` |
| F-08 | LOW | Editor reported success before storage | **Fixed & hardened** | `createTheme`/`updateTheme` await the printer; editor stays open on failure (Playwright probe: 500 → dialog open, no success toast, error shown) |
| F-09 | INFO | Size alarms close to hard limits | **Partially improved** | Locale alarm restored to 230 KiB for the pre-merge tree, then 240 KiB for the 38 upstream strings (hard limits unchanged); measured below |
| F-10 | INFO | Changelog wording/PR number | **Partly fixed** | Wording corrected; PR number/credit can only be added when the PR is opened |
| F-11 | INFO | 320 assets unreachable; firmwareChanged false without a manifest | **Fixed** | 64 KiB manifest bound and 64 KiB string arena; 320-asset manifest supported; `firmwareChanged` true when the manifest is unsupported |
| F-12 | INFO | Import not atomic across a network failure | **Documented** | Capacity is checked first; the residual behaviour is stated in `docs/APPEARANCE.md` |
| N-01 | LOW | Successful stale discard reported `display_firmware_changed` | **Fixed** | Error cleared after a successful discard (independent probe: `"error":""`) |
| N-02 | LOW | Negative stale-baseline case untested | **Fixed** | `test_display_theme.c` step 5d |
| N-03 | INFO | Baseline removal ignores unlink failures | **Accepted** | Fail-safe: leftovers are re-verified before use |
| N-04 | LOW | Status body too small for 320 long-named assets | **Fixed** | 64 KiB status buffer; 320-asset manifest with 60-character names passes |

### Earlier findings closed or accepted

| ID | Status | Note |
| --- | --- | --- |
| M-01 | Fixed | Unchecked `fopen`/short read in state; no crash |
| M-02 | Fixed | State lock not held across the upload; status answered during a half-sent upload |
| M-03 | Fixed | Backups atomic and verified against the recorded digest; corrupt backup refused |
| M-04 | Fixed | Token validation on every theme write/import |
| M-05 | Mostly fixed | Capacity pre-check; a mid-import network failure still leaves earlier writes (documented) |
| M-06 | Fixed | Per-item theme writes; document PUT merges by id |
| M-07 | Fixed | One 24-character rule; one-letter boundary included |
| M-08 | Fixed | Advanced icon; all tabs exercised |
| M-09 | Documented | `manage.sh` interaction and full hardware checklist |
| M-10 | Improved | Compact locales, gzip delivery upstream; measured sizes below |
| M-11 | Improved | Directory sync, idempotent recovery, journal removal after a successful rollback |
| L-01 | Fixed | Debug prints removed |
| L-02 | Fixed | Shared `<Page>` |
| L-03 | Fixed | Each colour field names its token |
| L-04 | Fixed | Boot id compared with the GUI marker |
| L-05 | Fixed | Directory fsync after rename/unlink |
| L-06 | Fixed | Accurate diagnostics and `unsupported` |
| L-07 | Partly fixed | PR number/credit pending by nature |
| L-08 | Fixed | 360 s client timeout against the server's 300 s window |
| L-09 | Fixed | Staged files removed on refusal/rollback |
| L-10 | Accepted | Shared 16 KiB bounded heap buffer; oversized appearance PUT is still a 400 after truncation (pre-existing) |
| L-11 | Fixed | Docs match the per-section recovery |
| L-12 | Accepted | `localStorage` writes on every edit; measured, non-blocking |
| L-13 | Fixed | System theme preview follows OS changes |

### New findings from this pre-PR audit (all fixed in `80033f1d`)

| ID | Severity | Problem | Fix and evidence |
| --- | --- | --- | --- |
| A-01 | **HIGH (blocker)** | Use-after-free in `ap_theme_delete_response`/`ap_profile_delete_response`: the id pointed into the freed JSON document. On a fresh daemon one `POST /profiles/delete` could SIGSEGV; ASan aborted the committed tests | The id is copied to a local buffer before `free(doc)`. ASan/UBSan builds now pass the appearance API, capacity, display API/capacity and the display harness; a loop of delete requests leaves the daemon alive |
| A-02 | **HIGH (blocker)** | `DISP_MAX_STAGE` 256 contradicted the advertised 320 assets: a 320-file pack was always refused and cumulative state entries could be dropped, leaving files unrestorable | `DISP_MAX_STAGE = DISP_MAX_ASSETS` (320); the display harness now builds, applies and restores a 320-file baseline/state (state ≈ 54 KiB < 64 KiB); the static limits test asserts the equality |
| A-03 | MEDIUM | The demo disabled `/api/appearance/themes/delete`, so the delete appeared to succeed then failed; the browser test only checked that a request was sent | Added to the demo allow-list and the test now asserts the theme is gone after a reload |
| A-04 | HIGH | Names were limited by characters on the client but by UTF-8 **bytes** on the printer: a 33-character Cyrillic name made the whole document save fail persistently | `fitName`/`nameOk` use UTF-8 bytes; all theme/profile/layout writes are fitted to 64 bytes; duplicate theme/layout names included |
| A-05 | — | `duplicateLayout` added " copy" without truncation | Folded into A-04; the name is fitted |
| A-06 | MEDIUM | Keyboard reorder dropped focus to `<body>` | The same button is refocused after the move (Playwright keyboard probe) |
| A-07 | MEDIUM | Profiles/Overview overflowed at 320 px with long names (up to 571 px) | `min-w-0` on the affected flex/grid chains; the browser test checks Themes, Profiles and Overview at 320/390 with a 64-character profile name |
| A-08 | MEDIUM | The theme editor confirmed before the printer stored the theme | The editor awaits the request and keeps the dialog open on failure (Playwright probe) |
| A-09 | MEDIUM | A failed slider save produced an unhandled promise rejection with no message | Slider saves catch and notify; the probe asserts a toast and no `unhandledrejection` |
| A-10 | MEDIUM | Focus fell to `<body>` after deleting a theme (opener removed) | The dialog focuses `main` when the opener is detached |
| A-11 | LOW | Palette labels and default names were hardcoded English | Translated through `t()` for dark/light/system, the default layout and "copy"; proper nouns stay |
| A-12 | LOW | A full import silently dropped invalid themes/profiles | The import compares declared and normalised counts and refuses with the import error |
| A-13 | LOW | A profile naming a missing custom theme showed a phantom palette | The server stores such a profile with the default palette; API test added |
| A-14 | LOW | A failed theme store mutated the live state (empty-id phantom theme) | All mutations happen on the local `next` state |
| A-15 | LOW | The display harness itself read out of bounds (partial journal literal) | Removed; the harness runs clean under ASan |
| A-16 | LOW | Startup recovery could use an uninitialised stage path | The path is initialised before the join |
| A-17 | INFO | `ng_member` could index one token past a full array | The loop stops at the used count |
| A-18 | LOW | 320 assets with long names exceeded the status buffer | 64 KiB status buffer; tested with 60-character names |
| A-19 | LOW | The 230 KiB locale alarm could not hold upstream's 38 new strings | Alarm 240 KiB (93.7 % of the unchanged 256 KiB hard limit); ARCHITECTURE numbers corrected |
| A-20 | INFO | `docs/ARCHITECTURE.md` quoted stale size alarms | Corrected to 940/240 KiB |
| A-21 | LOW | The shared tab component lacks `aria-controls`/roving tabindex | Pre-existing shared component; axe passes; not changed in this PR |
| A-22 | INFO | An import is not atomic across a mid-request network failure | Documented |
| A-23 | MEDIUM-HIGH | The layout **rename** field wrote the raw value, so a 33-character Cyrillic name (66 bytes) made every appearance save fail with 400 until shortened | The field now fits each keystroke to 64 UTF-8 bytes; the browser test types a 40-character Cyrillic name and asserts ≤64 bytes |
| A-24 | MEDIUM (edge) | A full 320-entry state silently skipped a new asset name (possible after a firmware renames assets), leaving the applied file unrestorable | The apply reserves a slot for every staged name before writing and refuses with `display_state_full`; a test fills the state and proves the target is unchanged |
| A-25 | LOW-MEDIUM | An import with an invalid or duplicate profile/layout was silently dropped while reporting "Imported." (the community fallback defeated the count check) | Each raw theme, profile and layout is validated against the normalised document; the browser test imports an invalid profile and asserts the error and no write |
| A-26 | LOW | Orphaned baseline files without a manifest were deleted without verifying the targets | The apply now refuses (keeps the files) when preserved files remain without a manifest |

---

## D. Test evidence

Environment: x86-64, gcc 16.2.1, Python 3.14.10, Node 26.13.1, Playwright
Chromium present, no `dash`, no BusyBox, no ARM toolchain, no printer.

### Backend

| Command | Result |
| --- | --- |
| `cd cc2-control && make clean && make CROSS= CC=gcc` | PASS, **0 compiler warnings** |
| 67 host scripts individually (the two Makefile-excluded payload tests and the C-harness driver omitted) | **67 passed, 4 failed only because `dash`/BusyBox are absent** (`test_binding_observation_retry`, `test_canvas_install_selection`, `test_managed_connections`, `test_pc_staging`); the same four fail at the merge base `a59ec640` on this host |
| `CC2_TEST_CFLAGS="-Wno-error=maybe-uninitialized" python3 tests/test_c_harnesses.py` | **1 747 PASS lines, exit 0**; the GCC-16 `maybe-uninitialized` warning in `test_mesh_api.c` is pre-existing (reproduced at the merge base) |
| `make CROSS= CC=gcc SANITIZE=address,undefined build/cc2-control` + `test_appearance_api`, `test_appearance_capacity`, `test_display_api`, `test_display_capacity` | **PASS under ASan+UBSan** (the first run reproduced the A-01 UAF; after the fix the same run is clean) |
| `test_display_theme.c` compiled with `-fsanitize=address,undefined` | **PASS**, including the 320-file apply/restore, restore-then-apply, stale discard (positive and negative), failed state write with rollback and no journal, responsive status and short Content-Length |
| Upstream tests present after the merge | `test_gzip_check.py`, `test_web_precompressed.py`, `test_installer_gzip_copies.py`, `test_http_browser_guard.py`, `test_spoolman.c` — all PASS |

### Frontend

| Command | Result |
| --- | --- |
| `npm run check` | PASS, 134 files (strict TypeScript + Biome) |
| `npm run build` | PASS; rebuilding leaves `git status` clean for `web/` (byte-identical) |
| `npm run test:appearance` | PASS (all tabs, per-item theme writes, one-letter id, delete persists across reload, layout/widgets, profiles, long-name phone widths) |
| `npm run test:display-pack` | PASS (3.42 MiB from the photo; oversized asset/tar refused) |
| `npm run test:printer-reports` | PASS (upstream ELEGOO fallback texts) |
| `npm run test:spoolman-integration` | PASS (includes the new on-demand sync test) |
| `npm run test:dashboard-style`, `test:obsidian-layout`, `test:preview` | PASS |
| axe-core audit of all 8 studio tabs at 1440/390, dark and light | **0 violations, 0 overflow** (32 combinations) |
| Playwright probes for the audit fixes | failed theme POST keeps the editor and shows the error; failed slider save shows a toast with no unhandled rejection; keyboard reorder keeps focus |
| Localisation | 11 files, **2 298 keys** each, identical key sets, no duplicate keys, placeholders preserved |

### Sizes

| File | Bytes | % of hard limit | Headroom |
| --- | ---: | ---: | ---: |
| `web/index.html` (before the Ember/Rose integration) | 950 527 | 90.65 % of 1 MiB | 12 033 B before the 940 KiB test alarm |
| `web/index.html` (final, addendum H) | **954 650** | 91.04 % of 1 MiB | 7 910 B before the 940 KiB test alarm; 93 926 B before the 1 MiB hard limit |
| `web/locales/ru.json` (largest) | **239 922** | 91.5 % of 256 KiB | 5 838 B before the 240 KiB test alarm; 22 222 B before the hard limit |
| other locales | 151 279–181 691 | 57.7–69.3 % | ≥ 64 KiB |

The locale alarm was 230 KiB before this integration; upstream added 38 strings
to every language (+≈5.8 KB in Russian), so the alarm is 240 KiB (93.7 % of the
hard limit) and the documentation says so. Hard limits are unchanged and the
backend still reads the plain files; gzip delivery reduces transfer, not the
plain file that must fit in memory.

### Builder

| Command | Result |
| --- | --- |
| `python3 experimental/pr128-integration/sync_source.py` on the committed tree | regenerated `source.zip`, `prepare.py`, `firmware_builder.py`, `run_build.py` |
| `test_cc2_control_source_snapshot.py`, `test_pinned_inputs.py`, `test_cc2_control_integration.py` | PASS; final `source.zip` sha256 `c4016b3e…`, pin `6b3e6611` (before addendum H: `f3e85e65…`, pin `76fbdba8`) |
| `python3 -m unittest discover -s builder/current/tests -p 'test_*.py'` | **Ran 61, 60 passed, 3 skipped, 1 failed only for missing `dash`/BusyBox** (`test_connections_firmware`) |

---

## E. Security assessment

Fresh audit of all external inputs and the changed file operations:

- **Memory safety**: the audit found and fixed a real use-after-free (A-01).
  ASan+UBSan now pass the appearance, manifest, state, tar/PNG and restore
  paths, including the 320-file pack. No double-free, leak (LeakSanitizer on a
  full request cycle), null dereference or uninitialised read remains in the
  exercised paths.
- **Input bounds**: JSON documents use bounded decoders (small 640-token and
  large 6 144-token/64 KiB arena variants, depth 16, no NUL, duplicate keys
  rejected); names/ids/colours are allow-listed; tar headers, sizes, names,
  duplicates, typeflag, link/prefix and end marker are checked; PNG signature,
  every chunk length/CRC, IHDR format/dimensions, IEND and exact consumption are
  checked; declared Content-Length is never exceeded.
- **Filesystem**: theme/state paths are fixed or allow-listed names without
  separators; `lstat` refuses symlinked/non-regular state/manifest/baseline
  files; writes go through temporary files with `O_EXCL`, `fsync`, `rename`
  and a directory sync; backups are verified against the recorded SHA-256
  before use; restore verifies all backups before copying any.
- **API**: the existing host/Origin/Sec-Fetch-Site/`X-CC2-Request` guard and
  the idle/update/maintenance barriers apply to every new route; no
  authentication boundary was changed. A LAN client can mutate by design (the
  project has no authentication), but only through the same guarded routes.
- **Recovery metadata**: a baseline is treated as valid only after its file
  digests match; a stale baseline is discarded only when the targets are
  provably originals; customised bytes keep the baseline and the refusal.
- **Residual**: the demo mock is not a security boundary; the shared tabs ARIA
  and localStorage write frequency are non-security quality notes.

No critical or high security finding remains.

---

## F. Hardware limitations (not verified)

Everything below needs a physical printer; none of it was tested and none may be
claimed:

1. The vendor init copies `cc2-display-manifest.json` from
   `/opt/inst/images/theme_default` to `/opt/usr/images/theme_default`.
2. The active theme directory is not re-synchronised on every boot (which would
   erase a customization at the reboot it requires).
3. `/opt/usr/images/current_theme` is an absolute symlink to the active theme.
4. The native GUI decodes the browser-produced RGBA PNGs (including the
   repeated 100-frame boot image) and boots when a replaced frame is unreadable.
5. Apply → full reboot → visible change; restore → reboot → original artwork.
6. Power loss during upload, backup, replacement, journal update, rollback and
   restore returns to a known-good state.
7. `experimental/native-display/manage.sh` behaves as documented after a real
   customization and restore.
8. Factory reset restores the expected defaults.
9. ARM cross-build/on-printer boot of the final sources (this host has no ARM
   toolchain).
10. Flash wear and the cost of the 15 s display-status poll on the printer.

Checklist for the user: the list above, executed on the exact shipping firmware,
with the printer idle and SSH access available; record results in
`docs/APPEARANCE.md` before any release.

---

## G. Recommended next step

The user may authorize opening the pull request. Before **release** (not before
the PR), run the hardware checklist in `docs/APPEARANCE.md` on a real printer,
add the PR number and contributor credit to the changelog entries, and decide
how to recover page/locale headroom for the next feature (the alarms are 940 KiB
and 240 KiB; the hard limits are unchanged). No push, PR or merge has been made
by this audit.

---

## H. Addendum — second upstream integration (Ember and Rose themes)

After this audit was written, `origin/develop` advanced once more, to
`b5968048` (*"Add Ember (dark) and Rose (light) themes"*, #453). Publishing the
audited tree as it stood would have left the branch one commit behind, with
conflicts in six files and, more importantly, with a feature that cannot store
the two new built-in palettes: the printer's validator did not know them, so
selecting Ember or Rose would have been refused by `PUT /api/appearance`.

That commit was therefore integrated before publication:

| Item | Value |
| --- | --- |
| Integrated upstream commit | `b5968048` (Ember/Rose themes, #453) |
| Conflicts | generated only (`source.zip`, `firmware_builder.py`, `run_build.py`, `prepare.py`, `web/index.html`) plus the real one in `preview/test-obsidian-layout.mjs`; generated files were rebuilt/regenerated, the test was combined |
| Theme contract | `ap_theme_id_valid()` (`src/appearance.h`) and `BUILTIN_TOKENS`/`LIGHT_THEME_IDS` (`web-src/src/lib/appearance.ts`) now carry `ember` and `rose` with the authoritative palettes from `web-src/src/index.css`; the gallery, `effectiveTokens`, accent linking and `selectTheme` follow the same list |
| Mode mapping | Ember is dark, Rose is light; `applyAppearance` derives the form-control colour scheme from `LIGHT_THEME_IDS`, so both custom and the newly light built-ins get `color-scheme: light` |
| Tests added | `test:obsidian-layout` asserts both themes persist through `/api/preferences` and `/api/appearance`, keep their stored mode and colour scheme across a reload, and are seen by a fresh browser; `test:appearance` asserts both cards in the gallery and Rose/Ember stored as light/dark |
| Counts | the studio now ships fourteen palettes; `CHANGELOG.md` and `docs/APPEARANCE.md` say fourteen |
| Snapshot | regenerated again from the final committed tree (`6b3e6611`), last: pin `6b3e6611`, `source.zip` sha256 `c4016b3e…` |

Verification after this integration (all re-run, not inherited):

- `test_theme_contract_static.py` PASS — 14 palettes agree between the C
  validator, the CSS blocks and the frontend list; `test_themes_static.py`
  PASS — 14 themes define the full token set; `test_appearance_static.py` PASS.
- `npm run check` PASS (134 files); `npm run build` reproduces the committed
  `web/` exactly; `test_web_sync_static.py` PASS (11 compact locales).
- Sizes after the integration: `web/index.html` 954 650 B (91.04 % of the 1 MiB
  hard limit; 7 910 B before the 940 KiB alarm), `web/locales/ru.json`
  239 922 B (91.5 %; 5 838 B before the 240 KiB alarm). Headroom is thinner
  than before because of the two extra palettes and their styles; the hard
  limits are unchanged.
- Backend: clean host build, `test_appearance_api`, `test_appearance_capacity`,
  `test_display_api`, `test_display_capacity` PASS on the merged tree.
- `test:obsidian-layout` PASS (theme selection/persistence/reload/fresh browser
  including Ember and Rose, layout sizing); `test:appearance` PASS (gallery,
  per-item writes, delete persistence, profiles, all tabs, phone widths).

The audit's verdict is unchanged: nothing in `b5968048` touches the display,
restore, import or capacity code the audit examined, and the theme contract gate
that would have caught an omission did fire and now passes.
