# Releasing

Checklist for publishing a release. A release is a Git tag plus the files attached to it. Published files are never replaced: a fix gets a new tag.

## Where the version lives

`cc2-control/VERSION` is the only place the CC2 Control version is written, and `cc2-control/FIRMWARE_VERSION` the only place the community firmware release number (`4.2`) is written. The Makefile passes both to the compiler, `installer/package.py` and `builder/current/prepare.py` read it, and `tests/test_version_static.py` fails if a copy appears in code. The release tag (`V4.2-R5`), the README download links and the changelog name a release and are edited by hand.

## Before tagging

1. Change `cc2-control/VERSION` if CC2 Control changes version, and `cc2-control/FIRMWARE_VERSION` if the firmware release number changes.
2. Update `CHANGELOG.md` ([Keep a Changelog](https://keepachangelog.com/en/1.1.0/) format): rename `## [Unreleased]` to `## [<release name>] - YYYY-MM-DD`, check that every user-visible change since the previous release has an entry, add a new empty `## [Unreleased]` above it, and update the link references at the bottom (`[Unreleased]` compares the new tag with `HEAD`, and the new release links to its GitHub release).
3. Refresh the builder snapshot, which must contain exactly the committed `cc2-control/` tree (`test_cc2_control_source_snapshot.py` fails otherwise). Commit everything else first, then on a clean tree run:
   ```sh
   python3 experimental/pr128-integration/sync_source.py
   ```
   It rewrites `builder/current/components/cc2-control/source/source.zip` and the pins in `builder/current/prepare.py` and `core/firmware_builder.py` (`SOURCE_COMMIT`, `SOURCE_SHA256`, `CC2_CONTROL_SOURCE_COMMIT`, `CC2_CONTROL_SOURCE_SHA256`), and the `BUILDER_SHA256` of `core/firmware_builder.py` in `builder/current/launch_helpers/run_build.py`. It pins `HEAD`, so commit the result as the last commit of the release branch and do not add commits that touch `cc2-control/` afterwards. Rebuild `cc2-control/web/` first if `web-src/` changed (see `AGENTS.md`).
4. Check the result: `python3 -m unittest discover -s builder/current/tests -p 'test_*.py'`.
5. CI is green on the release commit: host tests, sanitizers, ARM build, builder tests.
6. Fill in the real-printer checklist in [`docs/TESTING.md`](docs/TESTING.md) for this exact build: firmware hash, source commit, printer base version, date, tester, pass or fail per item. Keep it with the release notes.

## Tagging and publishing

1. The tag points at the commit that contains the exact source of the release.
2. Attach the firmware, the builder, the updater and the source archive, each with a `.sha256.txt` file.
3. Write the release notes: what changed, the checksums, the printer base version and what was tested on hardware.
4. Update the README download links and checksums.

## After publishing

Never replace or delete an attached file. If something is wrong, publish a new tag (for example `V4.2-R6`) and say in its notes what it replaces.
