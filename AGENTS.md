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

## Repository layout

| Path | Purpose |
|---|---|
| `cc2-control/src/` | CC2 Control C backend |
| `cc2-control/web/index.html` | Browser UI |
| `cc2-control/web/locales/` | UI translations, one JSON file per language (`en.json` is the source) |
| `cc2-control/tests/` | Host-side and integration tests |
| `builder/current/` | Current firmware builder and integration logic |
| `builder/current/tests/` | Host-side builder tests |
| `docs/` | Stable installation, build, architecture and testing documentation |
| `CHANGELOG.md` | User-visible release history |
| `releases/` | Lightweight historical release pointers where retained |

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

`cc2-control/web/index.html` is a large dependency-free HTML/JavaScript UI.
When changing it:

- inspect the complete relevant code path before editing;
- preserve English/Italian/French behaviour and persistent UI preferences;
- route new user-visible strings through `translatedText()` or `tpl()` and add
  the key to every `web/locales/*.json` (`test_locales_static.py` enforces parity);
- test the actual browser control or event path that changed;
- do not treat a direct function call or synthetic unit test as sufficient for
  interactive UI behaviour;
- verify the page against a running CC2 Control instance when practical.

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
