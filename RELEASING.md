# Releasing

Checklist for publishing a release. A release is a Git tag plus the files attached to it. Published files are never replaced: a fix gets a new tag.

## Where the version lives

`cc2-control/VERSION` is the only place the CC2 Control version is written, and `cc2-control/FIRMWARE_VERSION` the only place the community firmware release number (`4.2`) is written. The Makefile passes both to the compiler, `installer/package.py` and `builder/current/prepare.py` read it, and `tests/test_version_static.py` fails if a copy appears in code. The release tag (`V4.2-R5`), the README download links and the changelog name a release and are edited by hand.

## Before tagging

1. Change `cc2-control/VERSION` if CC2 Control changes version, and `cc2-control/FIRMWARE_VERSION` if the firmware release number changes.
2. Add the release to `CHANGELOG.md`.
3. Refresh the builder snapshot, which must contain exactly the committed `cc2-control/` tree (`test_cc2_control_source_snapshot.py` fails otherwise):
   ```sh
   git archive --format=zip -9 -o builder/current/components/cc2-control/source/source.zip HEAD:cc2-control
   sha256sum builder/current/components/cc2-control/source/source.zip
   ```
   Put the new SHA-256 in `SOURCE_SHA256` (`builder/current/prepare.py`) and `CC2_CONTROL_SOURCE_SHA256` (`builder/current/core/firmware_builder.py`). Commit, then set `SOURCE_COMMIT` / `CC2_CONTROL_SOURCE_COMMIT` and the commit in `builder/current/README.md` to that commit.
4. Put the SHA-256 of `core/firmware_builder.py` in `BUILDER_SHA256` (`builder/current/launch_helpers/run_build.py`).
5. CI is green on the release commit: host tests, sanitizers, ARM build, builder tests.
6. Fill in the real-printer checklist in [`docs/TESTING.md`](docs/TESTING.md) for this exact build: firmware hash, source commit, printer base version, date, tester, pass or fail per item. Keep it with the release notes.

## Tagging and publishing

1. The tag points at the commit that contains the exact source of the release.
2. Attach the firmware, the builder, the updater and the source archive, each with a `.sha256.txt` file.
3. Write the release notes: what changed, the checksums, the printer base version and what was tested on hardware.
4. Update the README download links and checksums.

## After publishing

Never replace or delete an attached file. If something is wrong, publish a new tag (for example `V4.2-R6`) and say in its notes what it replaces.
