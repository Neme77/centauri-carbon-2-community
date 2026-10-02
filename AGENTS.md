# Repository working rules

These rules apply to contributors and coding agents working in this repository.

## Canonical sources

- Treat `cc2-control/` and `builder/current/` as the canonical development
  sources.
- Use stable filenames and paths. Put release numbers in Git tags, GitHub
  Releases and `CHANGELOG.md` instead of duplicating version-specific
  documents on `main`.
- Keep historical snapshots and release artifacts read-only unless a change is
  explicitly intended for that historical version.
- Keep release history in GitHub Releases, tags and `CHANGELOG.md`; do not add
  duplicate release-pointer documents to `main`.

## Repository layout

| Path | Purpose |
|---|---|
| `cc2-control/src/` | CC2 Control C backend |
| `cc2-control/web-src/` | Browser UI sources (Vite + Preact + Tailwind, TypeScript) |
| `cc2-control/web/index.html` | Browser UI, the committed single-file build of `web-src/` |
| `cc2-control/web-src/public/locales/` | UI translations, one JSON file per language (`en.json` is the source); the build copies them to `cc2-control/web/locales/`, which the backend serves and the firmware ships |
| `cc2-control/tests/` | Host-side and integration tests |
| `builder/current/` | Current firmware builder and integration logic |
| `builder/current/tests/` | Host-side builder tests |
| `docs/` | Stable installation, build, architecture and testing documentation |
| `CHANGELOG.md` | User-visible release history |

## Change discipline

- Keep changes small enough to review.
- Pair behavioural changes with tests whenever practical.
- Avoid unrelated refactors in bug-fix changes.
- Preserve current safety guards, fail-closed behaviour and compatibility
  checks unless the change explicitly targets them.
- Do not rewrite public history for cosmetic cleanup.
- Keep commit messages concise; put detailed rationale and validation notes in
  pull requests or release notes.

## Build and test CC2 Control

For a native host build:

```sh
cd cc2-control
make clean test CROSS= CC=gcc
```

Cross-compiling for the printer requires the ARM toolchain documented in
`docs/BUILD.md`.

Host-side tests do not replace printer validation. Record real-printer testing
separately when a change affects hardware, MQTT, Canvas, camera, print control,
storage, motion or firmware integration.

## Firmware builder

`builder/current/` intentionally validates required inputs and pinned hashes.
Do not bypass missing-input, checksum or signing checks merely to make a build
complete.

Vendor firmware, private signing material and restricted third-party inputs
must not be committed. Preserve the provenance and licensing information in
`NOTICE.md` and `docs/SOURCE.md`.

Run the available host-side builder tests before changing build logic:

```sh
python3 -m unittest discover -s builder/current/tests -p 'test_*.py'
```

## Web UI changes

The UI is a Vite + Preact + Tailwind app in `cc2-control/web-src/`.
`cc2-control/web/index.html` is its committed, self-contained build: never edit
it by hand. After changing `web-src/`:

```sh
cd cc2-control/web-src
npm ci            # once
npm run check     # type-check (strict), lint and formatting (Biome); `npm run format` fixes formatting
npm run build     # rewrites ../web/index.html and ../web/locales/
```

- commit the rebuilt `web/index.html` and `web/locales/` with the sources (CI rebuilds it and fails
  on any difference) and keep the committed copy under
  `cc2-control/firmware-integration/overlay/.../web/` identical
  (`test_web_sync_static.py`);
- to add a colour theme, append an entry to `THEMES` in `web-src/src/lib/i18n.ts` and a `:root[data-theme="<id>"]` block with every token to `web-src/src/index.css` (`test_themes_static.py` checks it); the backend stores any lowercase-hyphenated identifier, so it needs no change;
- inspect the complete relevant code path before editing;
- preserve language behaviour and persistent UI preferences;
- route every new user-visible string through `t()` or `tpl()` with an identifier
  (`<group>.<name>`, e.g. `files.upload_file`; `common.` when several pages share it,
  `state.` for the machine states the backend sends in English). `web-src/public/locales/en.json`
  holds the English text and is bundled into the page; add the id to `en.json`, `fr.json`
  and `it.json` there (never edit `web/locales/`, the build overwrites it). `t()` only accepts ids from `en.json`, so a typo fails `npm run check`;
  reword the English text freely, the id does not change
  (`test_locale_coverage_static.py` and `test_locales_static.py` enforce the rest);
- register every periodic request with `poll()` or `usePoll()` from `web-src/src/lib/poll.ts` (never `setInterval`): the CC2 is resource-constrained, so the scheduler never overlaps runs of a source, sleeps in hidden tabs (except the OrcaSlicer pending-print check) and lets pages poll faster only while they are open (`test_polling_static.py`);
- test the actual browser control or event path that changed;
- do not treat a direct function call or synthetic unit test as sufficient for
  interactive UI behaviour;
- verify the page against a running CC2 Control instance when practical:
  `CC2_BACKEND=http://localhost:8099 npm run dev` serves the UI with hot reload
  and proxies `/api` and `/i18n` to that backend.

## Credentials and sensitive data

Never commit:

- printer LAN access codes;
- passwords or local configuration containing credentials;
- private keys;
- vendor firmware images;
- generated firmware/build output unless it is intentionally published as a
  GitHub Release artifact.

Use placeholders in documentation and test fixtures.

## Third-party components

Preserve third-party notices and do not imply ownership of vendor components.
Do not speculate about a vendor's intent when documenting publicly available
material; state only verifiable provenance.

## AI-assisted changes

AI-assisted work follows the same review, testing, safety and attribution rules
as any other contribution.

- Review generated or suggested code before committing it.
- Verify behavioural changes with the appropriate tests.
- Require real-printer validation where hardware behaviour is affected.
- Do not accept generated documentation that makes unsupported technical,
  licensing or provenance claims.
- Keep maintainers responsible for architecture decisions and release approval.
