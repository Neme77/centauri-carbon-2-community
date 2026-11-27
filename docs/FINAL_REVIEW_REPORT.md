# Independent final code review — Appearance customization

Review type: independent, read-only review of an existing implementation.
Reviewer role: senior maintainer / embedded Linux and firmware safety / frontend
architecture / QA.
Date: 2026-11-27.
Feature branch: `feature/appearance-customization`.
No commit, branch, pull request, release or printer was created or modified by
this review. The only file added to the repository is this report.

> **Remediation status.** The findings in this document were addressed in
> [`REMEDIATION_REPORT.md`](REMEDIATION_REPORT.md), which records each finding,
> its root cause, the change, the regression tests and the remaining risks.
> This file is preserved as the historical independent review and is not
> rewritten; where the two disagree, the remediation report describes the
> current implementation.

---

## 1. Review scope

The review covers the complete change set that the branch adds on top of
`develop`, as reported by its author:

- the versioned appearance document (`cc2-control/src/appearance.h`) and its
  HTTP API;
- the reversible native-touchscreen pack (`cc2-control/src/display_theme.h`),
  its capability detection, asset serving, tar/PNG validation, baseline,
  journal, rollback and startup recovery;
- the firmware-side display manifest
  (`builder/current/core/native_display_component.py`) and the builder pins it
  changes;
- the appearance studio, theme engine, dashboard layouts/widgets, profiles and
  import/export (`cc2-control/web-src/src/lib/appearance.ts`,
  `lib/display.ts`, `pages/appearance.tsx`, `pages/dashboard.tsx`,
  `pages/settings.tsx`, `lib/state.ts`, `lib/i18n.ts`, `main.tsx`,
  `index.css`, navigation);
- the committed web build (`cc2-control/web/index.html`, `web/locales/*`);
- eleven locale files;
- installer keep-list, application updater compatibility, documentation,
  workflow and tests.

Everything was reviewed against the repository's own rules in `AGENTS.md`
(canonical sources, design system, polling, locale parity, snapshot
regeneration, contribution discipline).

Out of scope: modifying the implementation, publishing anything, and physical
printer testing (no printer was available).

---

## 2. Repository baseline (established before any review)

| Item | Value |
| --- | --- |
| Working directory | `/mnt/encrypted/centauri-carbon-2-community` |
| Branch | `feature/appearance-customization` |
| HEAD | `fd246f5bd80cd639ae9802d6184ba12df0d96016` (`fd246f5b`) |
| Working tree | clean (`git status --porcelain` empty) before and after the review's own builds |
| Upstream base (merge base) | `a59ae640167c580a026ff915b224d1a899c8a560` |
| `origin/develop` at review time | `b7daa4418ba87935bbe437999d1c3565480529e9` |
| Branch position | 5 commits ahead, **2 commits behind** `origin/develop` |
| Merge base..HEAD | **69 files, +12 699 / −252** |
| Binary files in the diff | `builder/current/components/cc2-control/source/source.zip` (generated), `docs/images/appearance-studio.png` (documentation) |

The reported numbers (69 files, 12 699 insertions, 252 deletions) are correct.

Commits reviewed (oldest first), all authored by `PaperCompiler
<186202666+PaperCompiler@users.noreply.github.com>`:

| Commit | Subject |
| --- | --- |
| `23a5141f` | appearance: add versioned dashboard and reversible display APIs |
| `6d0f5bc4` | web: add the appearance studio, custom themes and dashboard layouts |
| `80ea11bd` | builder: publish a display manifest for touchscreen appearance |
| `2e1e0f58` | docs: describe the appearance studio and touchscreen scope |
| `fd246f5b` | build: refresh the CC2 Control source snapshot |

Findings about the baseline itself:

- **The branch is behind upstream and no longer merges cleanly.**
  `git merge-tree --write-tree --name-only origin/develop HEAD` reports content
  conflicts in `builder/current/components/cc2-control/source/source.zip`
  (binary), `builder/current/core/firmware_builder.py`,
  `builder/current/launch_helpers/run_build.py` and
  `builder/current/prepare.py`. These are exactly the shared generated/pinned
  files `AGENTS.md` warns about ("one change per pull request … regenerate the
  build and the snapshot after rebasing"). The branch's snapshot also predates
  develop's `cc2-control/src/spoolman.h`, `connections_http.h` and
  `connections/pin.h` changes, so a naive merge could ship an older backend.
- The source snapshot pin (`2e1e0f58`) is one commit behind HEAD, which is
  correct for this workflow (the snapshot commit follows the pinned commit) and
  `builder/current/tests/test_cc2_control_source_snapshot.py` passes.
- No vendor firmware, credentials, access codes or secrets were found in the
  diff. The documentation screenshot contains no printer address or LAN code.
- No build artifact was committed by mistake: `web/index.html` and
  `web/locales/*` are committed build outputs by design and are reproducible
  (verified: rebuilding produced byte-identical files and left the tree clean).
- Changes outside the intended scope that deserve scrutiny: the global
  `REQUEST_MAX` increase in `main.c` (12 KiB → 16 KiB) affects every endpoint,
  not only the new ones (see L-10).

---

## 3. Review methodology

1. Baseline: branch/HEAD/cleanliness/upstream/merge base, full diff, history,
   file classification (source, generated, documentation, binary), merge test
   with `git merge-tree`.
2. Full read of every changed source file (see the checklist in §16), plus the
   surrounding unchanged code that the new code calls (`main.c` request loop,
   `http_guard.h`, `connections/json.h`, `preferences_*`, `sha256.h`,
   `crc32.h`, `upload_slot_*`, `gcode_idle_for_mutation`,
   `maintenance_barrier_*`, `app_update_active`, installer and launcher
   scripts).
3. Independent build and test execution (x86-64 host; the repository's own Make
   targets and npm scripts). Background/parallel runs were repeated serially
   where they timed out.
4. Review-only probes, written outside the repository (in `/tmp/opencode`) and
   not committed:
   - a C harness that drives `disp_apply_start`/`disp_upload_thread` through a
     real socket (the committed harness calls `disp_receive_pack` directly and
     never exercises the HTTP streaming path);
   - HTTP probes for the appearance document capacity, restart round-trip,
     malformed input, and custom-theme identifier compatibility;
   - a probe that feeds the *real* builder-generated display manifest to the
     running binary;
   - a browser probe that measures the boot-frame pack size produced by the
     studio's own code, and axe-core audits of the studio.
5. Firmware-safety analysis: capability gating, identity checks, ordering of
   backup/journal/write/verify, durability, recovery paths, interaction with
   the existing `experimental/native-display` tool.
6. Severity classification per the task's scale, then a prioritised remediation
   list.

---

## 4. Security audit

### 4.1 What is solid

- The JSON decoder used for every new document (`connections/json.h`) is
  genuinely bounded: 640 tokens, 16 KiB string arena, nesting depth 16, no NUL
  in the input, strict UTF-8, no duplicate keys, finite numbers, no trailing
  data. Malformed, deeply nested, truncated and oversized documents are refused
  with 400; nothing was found that crashes it in the new code paths.
- Colour values are restricted to `#rrggbb`; theme, layout, profile and widget
  identifiers are allow-listed; names reject control characters; unknown fields
  are ignored and never written back. No HTML/CSS/JS escape hatch exists (the
  static test also guards this).
- Path traversal on `/api/display/asset` and in the upload is closed twice:
  URL decoding rejects `/`, `\` and control bytes, and `disp_name_valid()`
  rejects anything that is not `[A-Za-z0-9_.-]+.png` / `.` / `..`.
- The tar parser is careful: header checksum verified, only regular files
  (typeflag `0`), link name and GNU prefix must be empty, octal fields bounded
  against `SIZE_MAX`, size limited per file, names restricted to manifest
  entries, duplicates refused, end-of-archive marker and trailing bytes checked.
- PNG handling validates signature, every chunk length against the remaining
  buffer, every CRC, exactly one IHDR of 13 bytes, non-interlaced 8-bit
  truecolour/truecolour+alpha, the exact manifest dimensions, IEND presence and
  exact consumption of the file. Images are never decoded on the printer, so
  malformed compressed data cannot be executed and decompression bombs only hit
  the browser that the user controls.
- All new mutations go through the existing `http_browser_allowed()` guard
  (`Host`/`Origin`/`Sec-Fetch-Site`/`X-CC2-Request`), the existing
  `printer_change_refusal()` gates (maintenance, RFID, calibration, drying,
  nozzle expansion, updates) and the existing single-upload slot. No
  authorization boundary was weakened. As with every existing endpoint, any LAN
  client can perform the mutation by sending the marker header; that is the
  project's pre-existing trust model, not a new exemption.
- `appearance.json`, its `.bak`, the display state, baseline manifests and
  baseline copies are created `0600`; atomic writes use a temporary file,
  `fsync`, `chmod` and `rename`; `O_EXCL` prevents symlink following. The
  appearance loader refuses a non-regular file.

### 4.2 Findings

| ID | Severity | Title |
| --- | --- | --- |
| M-01 | MEDIUM | `disp_state_read()` dereferences a NULL `FILE*` when `state.json` exists but cannot be opened — SIGSEGV in the control service |
| M-03 | MEDIUM | Baseline restore trusts a preserved backup without re-verifying it against the recorded hash |
| M-04 | MEDIUM | A theme file imported through the studio is not token-validated before being applied to the live document |
| L-05 | LOW | The appearance and journal writes do not fsync the containing directory, so the durability claim is stronger than the code |
| L-06 | LOW | A newer-schema document is overwritten by the first save from an older client; parse failures are reported as "not valid JSON" |

Details:

**M-01 — unchecked `fopen` in `disp_state_read()`** —
`cc2-control/src/display_theme.h:384-388`:

```c
if (!disp_regular(path)) return 1;
FILE *file = fopen(path, "rb");
char body[4096];
size_t got = fread(body, 1, sizeof(body) - 1, file);   /* `file` may be NULL */
fclose(file);
```

Reproduction (verified, non-root, no printer): create a valid manifest and a
`display-theme/state.json` with mode `000`, then `GET /api/display`; the process
terminates with signal 11 (`exit code -11`). Trigger conditions are a race with
a file removal, EIO/ENOSPC, or a foreign tool leaving the file unreadable. The
service is supervised by procd with `respawn 3600 5 5`, so five crashes in an
hour leave CC2 Control down until a reboot. Every other loader in both new
headers checks `fopen`; this one does not.

**M-03 — restore does not verify the backup against `baseline.json`** —
`disp_restore_baseline()` copies each `baseline/<name>` through
`ap_copy_verified()`, which only proves that the copy equals the source. The
hash recorded in `baseline.json` (`entries[i].sha256`) is used for the
"is the current file ours?" check but never for the backup itself, and
`disp_backup_original()` returns success for an already existing backup without
hashing it. A corrupted or replaced baseline file is therefore written back to
the display directory as if it were the original, and the read-back comparison
passes. A malicious writer of the state directory is already root, so this is
an integrity/reliability gap, not a privilege boundary; it nonetheless defeats
the documented "verified baseline".

**M-04 — imported custom themes bypass token validation** —
`cc2-control/web-src/src/pages/appearance.tsx:1366-1375`:

```ts
if (parsed?.format !== 'cc2-theme' || typeof parsed.name !== 'string' || !parsed.tokens) throw Error()
const theme = createTheme(parsed.name, parsed.tokens as ThemeTokens, false)
```

`createTheme()` stores the object as given (unlike `importAppearance()`, which
runs `normalizeDoc()`). The 14 values are then written into
`document.documentElement.style` as CSS custom properties. The server rejects
the follow-up save with 400, so the printer document is safe, but the local
document (and the `cc2-appearance` cache) is left with invalid values, no
notification beyond the generic save error, and the page can render unstyled
until a reload (the cache normaliser drops the theme on reload). Low impact,
trivial fix: run `normalizeTokens()`/`normalizeThemes()` on the import.

**L-05 — directory durability**: `ap_write_all()` fsyncs the file but not the
parent directory, and the journal is removed with `unlink()` without a directory
sync. The claim "written atomically (temporary file, fsync, rename)" is true for
the file content; it does not guarantee that the rename survives a power loss.
Because the appearance document keeps `.bak` and the display feature keeps the
baseline, the practical risk is low, but the documentation should state it or
the code should `fsync` the directory.

**L-06 — downgrade and diagnostics**: a document with a newer `version` is
reported as `recovered`, served as defaults, and overwritten by the next save
(the old file survives only as `.bak`). The stored document that exceeds the
parser's token budget (see H-01) is reported as `"Appearance file is not valid
JSON"`, which is factually wrong and sends users looking for corruption instead
of "the document is too complex for this build".

---

## 5. Firmware and touchscreen safety findings

The design intent is good: manifest required, GUI hash checked, allow-listed
names only, idle printer, no process restart, backup before write, journal
before write, read-back after write, strict restore. The implementation,
however, cannot execute that model with the artefacts the project actually
ships, and the failure modes are not the ones the documentation describes.

### H-02 — the shipped display manifest is unreadable by the display subsystem (HIGH)

`builder/current/core/native_display_component.py:90-122` generates a manifest
with **154 assets** (100 `boot`, 4 `background`, 50 `accent`). The generated
document is **21 848 bytes and 1 703 JSON tokens**
(`NG_JSON_TOKENS` is 640 in `cc2-control/src/connections/json.h`). The size
guard in `disp_load_manifest()` (32 KiB) passes; `ng_json_parse()` then fails.

Verified against the running binary, with a manifest produced by the builder
module itself and a synthetic GUI with the matching SHA-256:

```
assets  supported  error
1       true       ""
50      true       ""
57      true       ""
58      false      display_manifest_invalid
100     false      display_manifest_invalid
154     false      display_manifest_invalid   <- what the firmware ships
```

Consequence: on every firmware built with this change, `GET /api/display`
answers `supported:false`, the studio shows "Not supported", and
`/api/display/apply` and `/api/display/restore` answer 409. **The headline
experimental feature cannot run on the firmware that introduces it.** The
committed tests only use one or two assets, so they pass.

### H-03 — baseline/state are also too large, and `state.json` is read into a 4 KiB buffer (HIGH)

Even with a small enough manifest to activate the feature:

- `disp_baseline_write()` writes one entry per preserved file; a full pack is
  154 files ≈ 770 tokens, above 640. `disp_baseline_read()` then fails, which
  breaks **restore, the failure rollback in `disp_receive_pack()`, and startup
  recovery** (`disp_startup_recover()` keeps the journal and logs "Display
  recovery failed"; every later apply is refused with "Display recovery is
  required").
- `disp_state_write()` builds the state into an **8 192-byte** buffer
  (`json_builder` fails when full), so recording more than ~74 applied files
  fails; `disp_receive_pack()` then treats the apply as failed *after* the files
  were replaced and attempts the rollback that H-03/H-02 also break.
- `disp_state_read()` reads `state.json` into `char body[4096]` with
  `fread(..., sizeof(body) - 1, ...)`. State for a realistic pack (more than
  ~37 files) is silently truncated and then fails to parse. After the required
  reboot the state is empty: `customizedCount` reports 0 and the strict restore
  refuses ("display_asset_foreign_change") for files that are in fact this
  feature's own writes.
- `disp_baseline_write()` (65 536-byte buffer) and the journal (32 768-byte
  buffer) are large enough for 154 entries; only the *parsers* of the baseline
  and the state are the blocking limits.

So the exact documents that make rollback possible are the ones that exceed the
parser, and the only in-process mitigation (the in-memory state cache) is lost
at the reboot the feature requires.

### H-04 — no supported recovery once the manifest/GUI identity changes (HIGH)

`disp_restore_response()` and `disp_startup_recover()` both require
`disp_capability()`, i.e. a *current* valid manifest, a matching GUI hash and a
valid active-theme link (`display_theme.h:1206-1215`, `1259-1264`). The baseline
itself records its own firmware base and GUI hash, but that information is never
used to allow a restore when the current manifest is missing or mismatched.

Scenario: apply a pack → firmware/panel update replaces
`/opt/usr/images/theme_default` or the GUI → the manifest no longer matches →
the API refuses restore (`display_gui_mismatch` / `display_firmware_changed` /
`display_manifest_missing`) and recovery refuses to run. The baseline files are
still on disk but there is no supported path to them. The pre-existing
`experimental/native-display/manage.sh restore` then refuses too, because it
requires each target to be its original or its own preview
(`files_match mixed`), and the studio's files are neither. The user is told to
"reflash".

The documentation says restoring "refuses if … the firmware display identity
changed after the backup", which is honest, but the feature's promise is a
*reversible* customization; an irreversible state is a safety property of the
whole design, not a UI detail. A restore should be allowed from a verified
baseline whose recorded identity matches the *baseline*, with an explicit
warning and the current manifest used only to confirm the target directory.

### H-05 — a realistic boot image produces a pack larger than the server accepts (HIGH)

The studio's own code was run in Chromium against a photo-like 1200×800 JPEG
(gradient, shapes, mild noise), using `bootFrame()` and `buildTar()` from
`web-src/src/lib/display.ts`:

```
input JPEG 286 875 B -> frame 53 771 B -> 100 frames = 5 377 100 B (5.1 MiB)
```

The server refuses any pack above 4 MiB (`DISP_MAX_PACK`) with the opaque
`Invalid display pack size`, while `bootFrame()` only refuses frames above
120 KiB (or 28 KiB if a coarser palette already fits). A pure-noise image is
refused client-side, but a normal photograph is accepted and then fails at
upload. The documentation's "very detailed images are refused" does not cover
this case, and the client never checks the total pack size before uploading.

### Recovery matrix (as implemented)

| Failure | Behaviour | Verdict |
| --- | --- | --- |
| Power loss during the upload, before the journal | No target was touched; staged files remain in `display-theme/stage/` until the next attempt (no cleanup). | Safe |
| Power loss while a baseline copy is being written | The backup is written with `O_EXCL`, but a power loss leaves a **partial file**. `disp_backup_original()` treats any existing regular backup as valid and returns success, and restore never compares it with the recorded hash (M-03). A truncated copy can be restored later as "the original". | **Gap** |
| Power loss during file replacement | The journal exists; the next start restores the baseline (fails for large baselines, H-03). | Intended, blocked by H-03 |
| Power loss after the first replacement but before the journal rename is durable | The journal may be absent; the state was not written yet, so `restore` sees files that are neither the baseline nor a recorded applied hash and refuses with `display_asset_foreign_change`. The half-applied customization is unrecoverable through the API. | **Gap** (L-05/M-11) |
| Corrupt backup | Trusted and copied back (M-03). | **Gap** |
| Missing manifest (removed, or firmware without it) | Capability false; apply and restore answer 409; startup recovery refuses and keeps the journal. The baseline is unreachable. | **Gap** (H-04) |
| Native GUI replaced / different firmware | Same as above; the recorded baseline identity is not used to permit a restore. | **Gap** (H-04) |
| Boot with a replaced image that the GUI cannot load | No mechanism: the journal is deleted on a successful write and only the reboot `notice` remains. Restore needs the control service to be up and the manifest to still match. | **Gap** |
| CC2 Control fails to start | No automatic action; the applied files stay on disk and the baseline stays in `/opt/usr/cc2-control/display-theme`. | Acceptable |
| Interrupted firmware update | The A/B OTA is unchanged; a re-synced theme directory makes the identity check fail closed. | Fail-closed |

### Other firmware-safety observations

- **Ordering is correct**: staging → idle/update/capability re-check → backup of
  every target → baseline manifest → journal → per-file atomic replace →
  read-back → state → journal removal. A failure before the journal touches no
  target; a failure after it is meant to be rolled back (subject to H-03).
- **TOCTOU window** (LOW): the idle check happens after the upload and before
  the writes, but backing up 154 files and writing them takes seconds. A print
  started in that window continues normally; the files only affect the next GUI
  start. Impact is a UX contradiction ("idle required"), not a print risk.
- **`sync()`** is called after the file replacements, before the journal is
  removed; directory entries are not synced (L-05).
- **Reboot notice** (L-04): `disp_status_response()` compares a
  `pid:starttime` marker and never the recorded `boot_id`, so a coincidental
  pid/start-time match after a reboot would leave "reboot required" visible.
- **Debug output left in production** (L-01):
  `cc2-control/src/display_theme.h:244,249,252,255,266,281` contain
  `fprintf(stderr, "DBG …")` lines without a newline, executed on manifest
  parse failures. This is clearly unfinished code.
- **Blocking the whole HTTP server** (M-02): `disp_mutex` is held for the whole
  upload (up to 300 s) and `disp_status_response()`/`disp_asset_response()`
  take the same mutex on the single request-processing thread
  (`main.c:6854-6862`). While an upload runs, an open appearance page (which
  polls `/api/display` every 15 s) blocks every other HTTP request, including
  print controls and the dashboard, until the upload finishes.
- **Existing tool conflict** (MEDIUM, M-09): once the studio has written a PNG,
  `experimental/native-display/manage.sh check|install` fails
  (`original_gui_or_images_differ`) and `restore` refuses
  (`RESTORE REFUSED: a target contains a foreign change`). The two systems must
  be coordinated or the interaction documented.
- **Firmware synchronisation assumption** (M-09): the manifest is installed into
  `/opt/inst/images/theme_default`; the runtime expects it in
  `/opt/usr/images/theme_default`. Whether the vendor sync that runs after an
  OTA copies an *arbitrary additional file* (and whether it overwrites
  `/opt/usr/images/theme_default` on every boot, which would erase the
  customization at the reboot it requires) is not verified anywhere and cannot
  be decided from the repository. It must be tested on hardware.

---

## 6. Touchscreen feature boundaries vs documentation

| Claim | Verified? |
| --- | --- |
| Accent recolouring of authored glyphs | Implemented and bounded (hue 145–200°, saturation ≥ 0.22, luminance preserved); runs in the browser. Cannot run on shipping firmware because of H-02. |
| Dark background tinting, clamped to a dark ceiling | Implemented in `tintBackground()`; same blocker. |
| Static boot artwork from a user image | Implemented, but a realistic image exceeds the pack limit (H-05). |
| No layout/widget/font/icon-set/native-program changes | True: the allow-list contains only presentation PNGs; no ELF is touched by the runtime path. |
| Unsupported operations are rejected, not silently accepted | True: names outside the manifest are refused; no endpoint accepts layout/font data. |
| Firmware without a manifest reports unsupported and refuses writes | True (host test and code inspection). |
| Firmware *with* the manifest the builder writes | **False** — it is reported as unsupported (H-02). |
| Reversible / rollback protects against supported failures | **Unproven and currently false** beyond small packs (H-03, H-04, M-03). |

---

## 7. Backend review

Findings:

- **H-01 (HIGH) — the appearance document cannot be read back at its documented
  capacity, and the user loses it on the next start.**
  `appearance.h` allows 6 profiles, 4 layouts of 9 widgets, 8 custom themes and
  32 KiB documents, but reads the file with the shared 640-token JSON parser.
  Measured with the real binary (review-only probes, HTTP only):

  ```
  saved configuration (accepted with 200)        stored size   after restart
  community + 2 extra profiles                    3 813 B      OK
  community + 3 extra profiles                    4 754 B      RECOVERED ("not valid JSON"), defaults
  community + 5 extra profiles                    6 671 B      RECOVERED, defaults
  4 layouts (9 widgets each) + 8 custom themes    5 841 B      RECOVERED, defaults
  ```

  The studio's UI allows all of these (profile limit 6, layout limit 4, theme
  limit 8) and each save answers `{"saved":true}`. After the next CC2 Control
  restart (or printer reboot) `GET /api/appearance` reports
  `recovered:true`, the Active profile falls back to `community`, the custom
  themes are gone and the classic layout is selected; the next save overwrites
  the file (keeping `.bak`). One stored profile is ≈157 tokens, so the 640
  budget is exhausted at four profiles. This is silent loss of user data at a
  configuration the product's own limits advertise.
  Root cause: two independent limits that were never tested together, plus a
  missing round-trip test at the documented maximum. A minimal guard would be
  "write the maximum document, restart, compare".
- **L-02 / architecture**: the document is a single monolithic object, while
  `POST /api/appearance/themes` and `.../profiles` are per-item. `PUT
  /api/appearance` replaces the whole `themes` array, so two browsers (desktop +
  phone) that both save will silently delete each other's themes; the profile
  endpoints are safe by contrast. No ETag/version check. See M-06.
- **M-07 (MEDIUM)**: `appearance.h` comments that a custom theme id "must be
  valid in both stores", but the legacy `/api/preferences` `theme` field accepts
  only 2–24 characters (`main.c:5125`), while the studio generates
  `custom-` + up to 28 characters (+ optional `-x`). Verified: `custom-` + 18
  characters is accepted by `/api/appearance` (200) and refused by
  `/api/preferences` (400). Choosing such a theme in Settings shows an error and
  leaves `ui-preferences.json` stale.
- The client and server disagree on identifier rules: the client `idOk()`
  accepts digits, repeated hyphens and a leading digit; the server's
  `ap_theme_id_valid()` rejects digits and `ap_id_valid()` rejects repeated
  hyphens. An imported theme can therefore be accepted by the browser and
  refused by the printer (400). Low functional impact, easy to align.
- `ap_load()` sets `ap_initialized` before it can succeed and never retries; a
  transient read failure leaves defaults for the process lifetime. Acceptable,
  but worth a comment.
- Error handling and messages are otherwise good and consistent with the
  project's style; validation is field-by-field and conservative.
- `REQUEST_MAX` was raised globally from 12 288 to 16 384 for the appearance
  document. No buffer was found that assumes the old limit, and the compiler
  reports no warnings, but the change affects every endpoint and doubles the
  per-pending-request buffer (8 × 16 KiB). A per-route limit would be cleaner
  (L-10).

### Architecture assessment

- **Separation** is good at the module level: presentation state
  (`appearance.h`), the on-disk display pack (`display_theme.h`), the browser
  model (`lib/appearance.ts`) and the browser transforms (`lib/display.ts`) have
  clear responsibilities, and the firmware builder owns the manifest.
- **The single-document model is the weak point.** One monolithic document with
  whole-array replacement for `themes` and (via export/import) `profiles`
  creates the concurrent-browser loss (M-06), the all-or-nothing import (M-05)
  and the capacity cliff (H-01). The per-item theme/profile endpoints show the
  better pattern. A future version should either expose per-item endpoints for
  layouts too, or add a document version/ETag so a stale client cannot overwrite
  newer state.
- **Duplicate functionality** exists between the new display pack and the
  pre-existing `experimental/native-display` tool/home-network patch: both write
  the same PNGs with their own pins, backups and restore logic, and each refuses
  the other's changes. Long term one of them should own the directory, or the
  two must share the baseline/identity model.
- **Complexity** is justified for the size of the feature, but the
  appearance/display limits are declared in three places (C header, TypeScript,
  documentation) and the JSON parser budget is a fourth, implicit one. That is
  the direct cause of H-01/H-02/H-03: no single place owns "what size is a valid
  document".
- **Future considerations** (not defects): community theme sharing would need a
  theme format/version and a provenance/licence field, and probably a signature
  or at least a warning; additional touchscreen assets and multiple printer
  models would need the manifest to carry a model/panel identity and per-model
  asset sets — the current `firmwareBase` constant (`02.01.01.07`, or
  `02.01.01.13` for the stock-13 stage) is a fixed string per build and is not a
  real model/version programme.


---

## 8. Frontend review

Strengths: the studio is coherent and well organised (overview, themes, layout,
widgets, touchscreen, profiles, transfer, advanced); the palette maths and
contrast reporting are real; custom themes are applied through the documented
14 tokens with no CSS injection; the classic dashboard remains the default; the
layout editor offers both drag-and-drop and keyboard arrow buttons; unsupported
touchscreen state is shown honestly; the demo mock answers the new routes.

Findings:

- **M-08 (MEDIUM)** — `appearance.tsx:1466` uses `icon="advanced"`, which does
  not exist in `components/icons.tsx`. In the demo/dev build opening
  **Appearance ▸ Advanced** throws `Undefined component passed to
  createElement()` and the page disappears; in the production bundle the tab
  renders but the card header has no icon. The new Playwright test
  (`preview/test-appearance.mjs`) never opens that tab, so CI cannot see it.
- **M-04 (MEDIUM)**, **M-06 (MEDIUM)** and **M-07 (MEDIUM)** above are also
  frontend issues.
- **M-05 (MEDIUM)** — `importAppearance()` applies a full document with a PUT
  and then one POST per profile. If the printer already holds six profiles, the
  PUT succeeds and the first extra profile POST returns 409; the user sees a
  generic error while the dashboard/themes have already been replaced. The
  import is therefore neither atomic nor clearly "merge" or "replace".
- **L-11 (LOW)** — the documentation says invalid fields fall back on load, but
  one invalid value inside `dashboard` discards the whole dashboard section
  (only the `recovered` flag remains). The API path is strict (400) as
  documented; the file path is stricter than documented.
- **L-12 (LOW)** — `cache()` writes the whole document to `localStorage` on
  every `updateDoc()` call, including every keystroke in the layout-name field
  and every colour-picker `input` event. It is synchronous and the document can
  be several kilobytes; a slider drag can cause jank on a phone.
- **L-02 (LOW)** — the Appearance page hand-writes its `<h2 class="text-2xl">`
  header instead of using the shared `<Page title sub>` component, contrary to
  the repository's design-system rule; it also omits `Page`'s
  `[overflow-wrap:anywhere]` on long titles.
- The drawer/navigation entry, routes and tab state follow the existing
  patterns; no polling was added outside the touchscreen tab, and `usePoll` is
  used correctly (no `setInterval`).
- `applyAppearance()` is called on every document change and sets/removes 14
  CSS variables, the density/font/motion data attributes and the legacy theme
  store; the cost is negligible. Theme switching performs no firmware
  operation, as claimed.

---

## 9. Accessibility findings

- Axe-core, injected into the studio in the demo: all eight tabs at 1440 px and
  390 px, dark and light → **no violations and no horizontal overflow**
  (32 combinations). The colour token editor's dialog, the touchscreen state,
  the profile list and the advanced tab were audited separately; a page-scoped
  audit of the dialog reproduced no violation on a second run (the first run
  reported colour-contrast nodes that did not reproduce; I do not count it as a
  finding).
- **L-03 (LOW)** — in the theme editor, 29 inputs share only 16 accessible
  names: the 14 hex text fields all use `appearance.color_hex` ("Colour hex
  value"), so a screen-reader user cannot tell *which* token they are editing.
  Give each field its token label.
- **L-13 (LOW)** — `Themes` calls `matchMedia()` during render without a
  listener, so the "Follow the system" preview does not update when the OS theme
  changes (the applied palette does, through the listener in
  `applyAppearance`).
- Keyboard navigation, focus trapping in dialogs, `role="switch"` with
  `aria-checked`, `aria-pressed` on theme cards, names on icon-only buttons,
  `role="alert"`/`role="status"` on the touchscreen warnings and the arrow
  alternative to drag-and-drop are all present. The global `:focus-visible` ring
  is untouched. No accessibility regression was found in the classic dashboard.
- Responsive: the studio has no horizontal overflow at 390 px and its tabs wrap;
  the grid CSS collapses 4 → 2 → 1 columns. The committed browser test checks
  390 px only; 320 px was not exercised (the demo's own navigation is checked at
  9 sizes by `test-browser.mjs`, which passes).

---

## 10. Performance and resource usage

- **Page size — headroom is nearly gone.** `cc2-control/web/index.html` grew
  from 854 283 to **938 762 bytes** (+84 479, +9.9 %), i.e. **89.5 % of the
  1 MiB hard limit, 109 814 bytes left**. The alarm in
  `test_web_size_limits_static.py` was raised from 900 KiB to 940 KiB for this
  change (the hard limit was correctly not raised). One more feature of this
  size will cross the limit and break the page (the backend answers 500 for an
  oversized index).
- **Locales — the Russian catalogue is at 92 % of its hard limit.**
  `web/locales/ru.json` grew from 218 482 to **241 328 bytes** (+22 846,
  +10.5 %); the hard limit is 262 144 bytes (the backend answers 500 above it,
  the client drops the catalogue) and the alarm was raised from 230 KiB to
  240 KiB, leaving **4 432 bytes** before the alarm trips. The next feature with
  a comparable number of labels will break Russian. This is not caused by the
  appearance feature alone, but the feature consumed most of the remaining
  budget: the recommendation is to compress locale delivery or split the
  catalogue before adding more strings, and to keep the alarm below 90 %.
- Touchscreen status is polled every 15 s while the tab is open, and each poll
  hashes every applied file (`disp_asset_count_customized`) while holding the
  display mutex. With a real 154-file pack that is several MiB of flash reads
  per poll. The tab is off by default and hidden tabs sleep, but the cost should
  be a state summary instead of re-hashing, or at least rate-limited.
- The studio's pack construction runs entirely in the browser, as intended; a
  full pack fetches, decodes and re-encodes 54 images plus 100 boot frames, and
  the browser cache keeps the decoded accents (a few MiB of `ImageData`).
- Theme switching is cheap and performs no printer work. The appearance
  document is cached in `localStorage` (see L-12 for the write frequency).
- Backend memory: the new state (`ap_state`) is ~18 KiB of BSS, the JSON parser
  ~36 KiB per parse (heap), the pending-request array grows by 32 KiB in total
  (heap) after the `REQUEST_MAX` change. Acceptable on the CC2.
- `main.c` compiles without warnings under `-Wall -Wextra -Wpedantic`.

---

## 11. Regression and compatibility review

- **Classic dashboard**: remained the default (`layout: "classic"`) and the
  preview test that pins the approved dashboard controls/layout passes. The
  refactor into widget components reproduces the previous composition.
- **Existing API consumers**: only additive routes plus two new fields under
  `system/info` (`appearance_profiles`, `touchscreen_appearance`) and two
  endpoint entries. `REQUEST_MAX` changed globally (L-10). The `/api/preferences`
  contract is unchanged, and a custom theme that fits 24 characters still
  persists. No existing route was removed or reordered.
- **Migration from `ui-preferences.json`**: on first run the appearance document
  inherits the stored theme, so the palette does not change on upgrade; a
  missing, invalid or partially written legacy file falls back to `dark`;
  repeated migration is idempotent (no file is written until the user saves).
  `loadUiPreferences()` deliberately avoids overwriting the cached appearance.
  Verified for the valid-theme case by the committed test and by inspection for
  the others.
- **Downgrade**: an older CC2 Control without this feature ignores
  `appearance.json` and keeps using `ui-preferences.json`, which still holds a
  theme that the studio writes only when the theme is changed from Settings
  (see below). A newer-schema document is treated as recovered and can be
  overwritten (L-06).
- **Installer / application update**: `restore-on-printer.sh` keeps
  `appearance.json`, `appearance.json.bak` and the whole `display-theme`
  directory (both directions); the in-place app installer does not delete them.
  `test_installer_package.py` was updated and passes.
- **Klipper/Moonraker/camera/MQTT/network/print control**: untouched; the new
  code only reads the MQTT state and the existing idle/update barriers.
- **Existing native-display tool** (`experimental/native-display/manage.sh`):
  conflicts with studio-written files (M-09).
- **Legacy theme write claim**: `docs/APPEARANCE.md` says "The legacy
  `/api/preferences` theme identifier is still written for compatibility". The
  studio updates the in-memory legacy store on every change but only *persists*
  to `/api/preferences` from Settings; the appearance document is authoritative
  and `loadUiPreferences()` ignores the legacy value when a cache exists. The
  outcome is consistent for this release, but the documentation overstates what
  the studio writes.

---

## 12. Test results (independently reproduced)

Environment: Linux x86-64, gcc 16.2.1, Python 3.14.10, Node 26.13.1, npm 12.2.2,
Playwright Chromium present. No ARM cross-compiler, no `dash`, no BusyBox, no
physical printer.

| Command | Result |
| --- | --- |
| `cd cc2-control && make clean test CROSS= CC=gcc` | build succeeds with **0 warnings**; the suite stops at exit 2 in `tests/test_binding_observation_retry.py` because the host has no `dash` (environment, not implementation) |
| same tests run one by one, 64 scripts (2 excluded by the Makefile: `test_rfid_app_boot.py`, `test_native_signed_payload.py`) | **59 pass**, **4 fail only for the missing `dash`/BusyBox** (`test_binding_observation_retry`, `test_canvas_install_selection`, `test_managed_connections`, `test_pc_staging`), 1 (`test_c_harnesses`) exceeded the review's 900 s cap and was rerun separately (below) |
| `python3 tests/test_c_harnesses.py build/cc2-control` (rerun, serial) | see §12.1 — the set stops at `test_mesh_api.c`, which fails to compile with **GCC 16** on a pre-existing `-Wmaybe-uninitialized` warning; the same failure occurs at the merge base, so it is not caused by this branch |
| `cc … tests/test_display_theme.c … -Werror` (the new display harness, compiled separately with the repository's exact flags) | compiles with `-Werror` and **PASS** |
| `python3 -m unittest discover -s builder/current/tests -p 'test_*.py'` | **Ran 61 tests, 60 pass, 3 skipped, 1 failure** — `test_connections_firmware.test_actual_bootstrap_success_refusal_and_rollback` requires `dash` and `busybox` (environment, unrelated to this feature) |
| `cd cc2-control/web-src && npm run check` | exit 0 (`tsc` strict + Biome, 134 files) |
| `npm run build` | exit 0; rebuilt `web/index.html` and `web/locales/` are **byte-identical** to the committed files (`git status` clean afterwards) |
| `npm run test:appearance` | PASS |
| `node preview/test-dashboard-style.mjs` | PASS |
| `node preview/test-obsidian-layout.mjs` | PASS |
| `node preview/test-browser.mjs` | PASS (all pages, 9 viewport sizes) |
| `python3 tests/test_web_sync_static.py`, `test_locales_static.py`, `test_locale_coverage_static.py`, `test_themes_static.py`, `test_web_size_limits_static.py`, `test_polling_static.py` | PASS |
| axe-core audit of the studio (32 combinations) | no violations, no overflow |
| review-only: HTTP streaming apply harness | PASS (apply over a real socket, traversal refusal, truncation refusal with no target change and no journal, idle gate, 4 MiB bound, missing Content-Length) |
| review-only: appearance capacity/restart probes | **failures found** — see H-01 |
| review-only: real builder manifest against the binary | **failure found** — see H-02 |
| review-only: boot-frame size measurement | **5.1 MiB pack from a normal photo** — see H-05 |
| review-only: unreadable `state.json` | **SIGSEGV** — see M-01 |

The reported "1,814 passing host-side checks" could not be reproduced as a
single number because `make test` aborts on the first environment-specific
failure; at script level 59 of 64 pass plus the C harness set, and every
failure recorded here is an environment dependency (shells/toolchain) except
the findings listed in this report.

### 12.1 C harness set

`tests/test_c_harnesses.py` compiles and runs ~120 standalone harnesses,
including `tests/test_display_theme.c` (valid pack, unknown name, duplicate,
corrupt PNG, dimension mismatch, baseline restore, foreign change, firmware
identity change, interrupted-apply recovery). On this host the loop printed
**726 PASS lines** and then stopped at `test_mesh_api.c`, which does not compile
with GCC 16 and `-Werror` (`'e1' may be used uninitialized` in
`tests/test_mesh_api.c:77` / `src/main.c:4078`). The identical failure was
reproduced against a pristine `git archive` of the merge base `a59ae640`, so it
is a pre-existing test/compiler compatibility issue and not part of this change.
Because the runner aborts, the harnesses listed after `test_mesh_api` (including
`test_display_theme`) were not reached in that pass; the new display harness was
therefore compiled and run directly with the repository's exact flags
(`-O2 -std=c11 -Wall -Wextra -Wpedantic -Werror -pthread`): **it builds clean and
passes**. The remaining harnesses are unrelated to this feature and were not
re-run individually.

The committed display harness itself does **not** cover the HTTP streaming path;
the review-only harness in `/tmp/opencode` covered it and passed.

### 12.2 Coverage gaps found

- No test writes the documented maximum appearance document and reloads it
  (H-01).
- Display tests use one or two assets; nothing exercises the 154-asset manifest
  or a full 100-frame pack (H-02/H-03).
- The HTTP apply path (streaming, thread, response, idle gate under real
  sockets) was untested in the repository.
- The new Playwright test does not open the Advanced tab (M-08).
- No test for a corrupt baseline, a missing manifest after an apply, a
  downgrade, or two concurrent browsers.
- No axe test for the studio (the repository has the `CC2_AXE_PATH` pattern
  elsewhere).

### 12.3 Second-pass re-verification

The branch, HEAD (`fd246f5b`), merge base and `origin/develop`
(`b7daa441`) were unchanged on a second baseline check, so this report was not
revised for new upstream commits. The three decisive findings were re-run
against the same host build:

- H-01: four stored profiles (4 754 B) → after restart `recovered:true`,
  `error:"Appearance file is not valid JSON"`, 1 profile, 0 themes, 2 layouts —
  reproduced.
- H-02: the real builder manifest (154 assets, 21 848 B) → `supported:false`,
  `error:"display_manifest_invalid"` — reproduced.
- M-01: an existing but unreadable `state.json` → the service dies with signal
  11 on `GET /api/display` — reproduced.

---

## 13. Hardware testing limitations

No physical printer was available and none was touched. Therefore the following
remain **unverified claims**, not facts:

- that the vendor init copies `cc2-display-manifest.json` from
  `/opt/inst/images/theme_default` to `/opt/usr/images/theme_default`;
- that `/opt/usr/images/theme_default` is not re-synchronised on every boot
  (which would erase a customization at the reboot it requires);
- that `/opt/usr/images/current_theme` is an absolute symlink to
  `theme_default` (the existing `manage.sh` compares it to the absolute
  `IMAGES` path, which suggests it is; only hardware can confirm);
- that the native GUI decodes the browser-produced RGBA PNGs identically to the
  vendor originals;
- that the GUI recovers if a replaced boot frame is unreadable;
- that the 154-asset manifest is even reachable (H-02 says it is not);
- boot-time duration, flash wear and the real cost of the 15 s status poll.

The documentation's "Outstanding hardware tests" section lists some of these;
it should list all of them, and the changelog/PR description must not claim
printer validation.

---

## 14. Upstream contribution readiness

| Check | Result |
| --- | --- |
| License / provenance | Browser code is project code; the manifest and the native-document PNGs remain vendor-derived/`experimental/native-display` assets. No new third-party asset, font or binary was added. `docs/APPEARANCE.md` states the provenance correctly. |
| Author attribution | All five commits by `PaperCompiler`; the changelog credits the same name. |
| Secrets / credentials / printer data | None found. |
| Generated files | Only the expected `source.zip`, `web/index.html` and `web/locales/*`. |
| Dependencies | One new dev script only (`test:appearance`); no new runtime dependency. |
| Commit history | Small and scoped; the snapshot is the last commit, as required. |
| Changelog | Two entries under `[Unreleased] → Added`, but **without a PR number**, and the text claims settings are "preserved across … reboots" and the touchscreen baseline "restores on failure", which the findings contradict (L-07). |
| Documentation | `docs/APPEARANCE.md` is thorough and unusually honest about scope and hardware tests; `API.md` is accurate about the contract, but both describe a limit set the parser does not honour (H-01) and a manifest the parser cannot read (H-02). |
| Mergeability | **No** — 4 conflicting generated/pinned files against `origin/develop`; snapshot regeneration after rebase is mandatory (H-06). |
| PR claims | The reported "successful checks" are reproducible for the parts that run; the claim of a working optional touchscreen customization is not, and "1,814 passing checks" is not reproducible as stated. |

---

## 15. Complete findings list

Legend: **B** = blocks upstream submission.

### HIGH

**H-01 (HIGH, B) — Appearance document accepted at its documented capacity is
unreadable after a restart; user themes/layouts/profiles are silently lost**
File: `cc2-control/src/appearance.h` (limits at 22–27; `ap_load` at 701–813),
`cc2-control/src/connections/json.h:7` (`NG_JSON_TOKENS 640`).
Evidence/reproduction: save three extra profiles (4 total) or 4 layouts + 8
themes through the API (all answer 200); restart the binary; `GET
/api/appearance` answers `recovered:true`, `error:"Appearance file is not valid
JSON"`, one profile, classic layout, no themes. Stored size 4 754 B / 5 841 B
(below the 32 KiB guard).
Impact: silent loss of user configuration on the next reboot for a
configuration the UI offers.
Correction: raise the document parser budget (a dedicated parser with a
document-appropriate token count, not a global change to the shared decoder),
or lower the advertised limits so the worst case fits; add a round-trip test at
the maximum that restarts the process; report "document too complex" instead of
"not valid JSON".

**H-02 (HIGH, B) — The display manifest the firmware builder ships cannot be
parsed; touchscreen customization is never available**
File: `builder/current/core/native_display_component.py:90-134`,
`cc2-control/src/display_theme.h:201-289`.
Evidence: generated manifest = 154 assets, 21 848 B, 1 703 tokens; the parser
accepts ≤57 assets. Running the binary with the real manifest reports
`supported:false, error:"display_manifest_invalid"`.
Impact: the feature's headline capability does not work on the firmware that
introduces it; all touchscreen tests are therefore testing an unreachable path.
Correction: parse the manifest with a baseline-appropriate bounded structure
(e.g. a flat record list with a fixed array, or a much larger dedicated token
budget) and add a test that loads the *real* generated manifest; alternatively
reduce the asset set, but 100 boot frames are inherent to the design.

**H-03 (HIGH, B) — Baseline and state documents exceed the same limits; the
state is written in an 8 KiB buffer and read in a 4 KiB buffer — rollback,
recovery and restore fail when they are needed**
File: `cc2-control/src/display_theme.h:378-421` (4 096-byte read), `423-452`
(8 192-byte write), `499-535`, `594-630`, `1037-1042`.
Evidence: 154 entries × ~5 tokens > 640 for `baseline.json`; the state write
fails above ~74 entries (8 192-byte `json_builder`), which aborts an apply after
the files were replaced; state entries above ~37 files are truncated by
`fread(body, 1, 4095, file)`.
Impact: a failed apply cannot roll back; startup recovery refuses and keeps the
journal (blocking every later apply); after the required reboot the state is
empty, so `customizedCount` is wrong and strict restore rejects the feature's own
files as foreign.
Correction: same parser fix as H-02; size the state read from the file
(`stat` + dynamic buffer, bounded), and add a pack-level test with ≥100 files
that interrupts an apply.

**H-04 (HIGH, B) — Restore and recovery require the *current* manifest/GUI to
match, so a firmware change makes the baseline unreachable; the existing
`manage.sh` then also refuses**
File: `cc2-control/src/display_theme.h:1206-1215, 1250-1274`;
`experimental/native-display/manage.sh:69-114`.
Impact: an irreversible customization after a panel/firmware update; the only
advice left is reflashing.
Correction: allow a restore from a verified baseline whose own recorded identity
is consistent, with an explicit warning and a target-directory re-check; document
the interaction with `manage.sh`.

**H-05 (HIGH, B) — A realistic boot photo builds a 5.1 MiB pack against a 4 MiB
server limit; the client accepts the image**
File: `cc2-control/web-src/src/lib/display.ts:156-190, 236-272`;
`cc2-control/src/display_theme.h:34, 1169-1172`.
Evidence: measured 53 771 B per 532×300 frame from a photo-like JPEG → 100 frames
= 5 377 100 B.
Impact: the advertised "custom boot artwork" fails for normal images with an
opaque `Invalid display pack size`.
Correction: bound the frame to `DISP_MAX_PACK/100` minus the other assets in
`bootFrame()` (or compress/quantise harder), and check the assembled pack size
before uploading with a clear message.

**H-06 (HIGH for submission) — The branch does not merge into current
`develop` and its snapshot omits upstream backend changes**
Evidence: `git merge-tree` conflicts in `source.zip`, `firmware_builder.py`,
`run_build.py`, `prepare.py`; 2 commits behind; develop touched
`cc2-control/src/spoolman.h`, `connections_http.h`, `connections/pin.h`.
Correction: rebase, resolve, re-run `experimental/pr128-integration/sync_source.py`
on a clean tree, regenerate `web/index.html`, and commit the snapshot last.

### MEDIUM

- **M-01** Unchecked `fopen` in `disp_state_read()` → SIGSEGV on
  `/api/display` (verified).
- **M-02** `disp_mutex` held across the whole upload and taken by the status
  handler on the single HTTP thread → the whole control service blocks for up to
  300 s while an appearance page polls.
- **M-03** Baseline restore does not verify the backup against
  `baseline.json`'s hash, and a *partial* backup left by a power loss is treated
  as a valid original forever (see the recovery matrix).
- **M-04** Imported theme tokens are not validated before reaching the live CSS
  custom properties.
- **M-05** Document import is not atomic (PUT then per-profile POSTs; a 409
  leaves a partial import).
- **M-06** Whole-array `themes` replacement in `PUT /api/appearance` lets two
  browsers silently delete each other's themes.
- **M-07** Custom theme ids longer than 24 characters are accepted by the studio
  and refused by `/api/preferences` (verified), contradicting the code comment.
- **M-08** `icon="advanced"` is missing from the icon map; the Advanced tab
  throws in dev/demo and has no icon in production; the new test never visits it.
- **M-09** No hardware validation of manifest deployment/persistence, and the
  existing `experimental/native-display` tool refuses once the studio has
  written.
- **M-11** A successful apply deletes the journal before any confirmation that
  the new files load, and the journal's rename is not directory-synced: a power
  loss in that window can leave a partially applied customization that the
  strict restore refuses as "foreign".
- **M-10** Locale/page size headroom nearly exhausted (ru.json at 92 % of the
  hard limit after +22.8 KB; index.html at 89.5 % after +84.5 KB).

### LOW

- **L-01** `fprintf(stderr, "DBG …")` debug lines in
  `display_theme.h:244,249,252,255,266,281`.
- **L-02** Hand-written page title instead of `<Page>`; no `overflow-wrap` on the
  long title.
- **L-03** 14 hex inputs share one accessible name.
- **L-04** Reboot notice compares a pid/start-time marker but never the recorded
  `boot_id`.
- **L-05** No directory fsync after rename/unlink (durability claim).
- **L-06** Newer-schema documents are overwritten on save; parse failures are
  reported as "not valid JSON".
- **L-07** Changelog entries lack a PR number and overstate persistence/rollback.
- **L-08** Client XHR timeout (300 s) equals the server budget; a pack that
  completes may be reported as failed.
- **L-09** A refused pack leaves staged files until the next attempt.
- **L-10** Global `REQUEST_MAX` increase for one feature.
- **L-11** Documentation says invalid fields fall back on load; one bad field
  discards the whole dashboard section.
- **L-12** Synchronous `localStorage` write of the whole document on every input
  event.
- **L-13** "System" theme preview does not follow OS changes live.

### INFO

- Consider per-item endpoints (or an ETag/version) for themes and layouts to
  avoid the whole-document replacement problem.
- Consider a compatibility/version field on custom themes and a signature or
  provenance note before community theme sharing, as future work.
- Theme identifiers travel through two stores with different grammars; one
  shared validator would remove the class of bugs behind M-07.
- `/api/appearance` and `/api/display` could return 405 (not 404) for
  unsupported methods, consistent with newer routes.
- The display feature is labelled experimental and is correctly gated in the
  UI; that mitigates H-02/H-03 for users but not for the safety claims.

---

## 16. Review checklist

- **All changed files inspected:** yes — 67 text files read (source, tests,
  docs, locales sampled for parity and quality, workflow), 1 generated binary
  verified through its pin and snapshot test, 1 documentation image inspected.
- **All new APIs reviewed:** yes — 10 routes (method, body, bounds, error
  codes, guards, concurrency).
- **All filesystem write operations reviewed:** yes — `appearance.json`,
  `.bak`, temporary files, display state/journal/baseline/stage, theme-file
  replacement, installer/keep-list, snapshot generation.
- **Archive and image handling reviewed:** yes — tar header/type/size/name/
  checksum/end marker; PNG signature, chunks, CRCs, IHDR limits, dimensions;
  the browser-side transforms and pack assembly.
- **Rollback mechanisms reviewed:** yes — apply failure rollback, startup
  journal recovery, strict restore, `manage.sh` interaction, installer rollback
  and its keep-list.
- **Existing functionality regression-tested:** yes to the extent the host
  allows (classic dashboard, existing API, preferences migration, 59 host test
  scripts, builder tests, three modified browser tests, rebuild
  reproducibility). No physical printer.
- **Full host test suite passed:** **no** — 59/64 scripts pass, 4 fail only
  because `dash`/BusyBox are missing on this host, the C harness set stops at a
  pre-existing GCC-16 warning in `test_mesh_api.c`, and the new display harness
  passes when compiled and run directly. The suite cannot be claimed green
  here.
- **Physical hardware tested:** **no**. No printer was available or touched.
- **Outstanding risks:** H-01…H-06, M-01/M-02/M-09 and the hardware assumptions
  in §13 remain.

---

## 17. Overall assessment

**REQUIRES SIGNIFICANT CHANGES — not ready for upstream submission as it
stands.**

Why:

1. Two independent, reproducible high-impact defects make the two headline
   capabilities unusable at their advertised limits: the appearance document
   loses all user configuration on the next restart at four profiles (H-01), and
   the touchscreen pack cannot activate on the manifest the firmware ships
   (H-02), with its rollback documents exceeding the same parser budget (H-03)
   and its only recovery path gated on the same capability check (H-04).
2. A normal boot photo produces a pack the server rejects (H-05).
3. The branch is not mergeable with current `develop` and its snapshot predates
   upstream backend changes (H-06).
4. No printer test exists for a subsystem that writes to the printer's display
   resource directory, and several of its safety claims depend on unverified
   vendor behaviour.

What is genuinely good: the security posture of the new inputs (bounded parser,
allow-lists, CRC/header validation, origin guard, atomic writes, fail-closed
capability detection), the design-system-consistent studio, the honest
documentation of scope, the localisation quality and the test discipline that
does exist. The remaining work is well bounded and mostly mechanical, plus one
architectural decision (how to read documents larger than the shared 640-token
parser budget).

---

## 18. Prioritised remediation list

1. **Fix the document size model (H-01/H-02/H-03).** Decide the maximum
   appearance document and the maximum display manifest/baseline/state, and use
   a parser whose budget matches. Preferred: a dedicated, bounded parser for the
   manifest/baseline/state (flat record arrays), and either a larger
   appearance-only budget or lower advertised limits. Do not raise
   `NG_JSON_TOKENS` globally without checking every existing caller's stack and
   heap use.
2. **Add round-trip tests at the real maxima**: maximum appearance document →
   restart → compare; real 154-asset manifest → `supported:true`; 100-frame pack
   → apply, interrupt, restart, recover, restore; corrupt/missing state; two
   concurrent browsers.
3. **Fix the crash** (M-01) and the 4 KiB state read (H-03).
4. **Make recovery independent of the current manifest** (H-04): restore from a
   verified baseline with its own recorded identity, and document the
   `manage.sh` interaction.
5. **Bound the boot pack in the client** (H-05) and check the total before
   upload.
6. **Fix the Advanced tab icon** (M-08) and extend `test-appearance.mjs` to all
   eight tabs (and to the theme editor dialog).
7. **Fix identifier compatibility** (M-07) with one shared validator, and
   validate imported theme tokens (M-04).
8. **Do not hold the display mutex across the upload** (M-02); serve status from
   a snapshot.
9. **Rebase onto current develop, resolve the four generated/pinned files,
   regenerate the snapshot and the web build, then commit the snapshot last**
   (H-06).
10. **Trim the size budget**: recover locale/page headroom or arrange compressed
    delivery before the next feature (M-10); keep the alarm below 90 %.
11. **Verify on hardware**: manifest copy and persistence, symlink target, GUI
    decoding of replaced PNGs, recovery after an interrupted apply, and one
    apply/restore cycle on the exact shipping firmware. Record the results in
    `docs/APPEARANCE.md`.
12. **Finish the small items**: remove the `DBG` prints (L-01), use `<Page>`
    (L-02), distinct hex-field names (L-03), boot-id comparison (L-04),
    directory fsync (L-05), misleading error text (L-06), changelog wording and
    PR number (L-07), client timeout (L-08), staging cleanup (L-09).
13. **Correct the documentation claims** that the current code does not support
    (persistence across reboots at any allowed capacity, "restores on failure",
    "the legacy identifier is still written"), and add the hardware-test
    checklist to the PR description.

---

*End of report.*
