# Contributing

Contributions, test reports and focused bug fixes are welcome.

## Before opening a change

1. Reproduce the issue against the current `develop` branch and state the exact firmware base and panel version.
2. Keep one logical change per pull request.
3. Add or update a regression test where practical.
4. Run the CC2 Control test suite and the relevant builder tests.
5. Document any behaviour, protocol or installation change.

## Author responsibilities and reference guidelines

PR authors must document and test their own changes before requesting a
merge. Follow this guide, [AGENTS.md](AGENTS.md), [Testing](docs/TESTING.md)
and [Releasing](RELEASING.md). CI supplements the author's evidence; it does
not replace it or transfer qualification work to the maintainer.

Every PR must state the problem and reproduction, the resulting behaviour,
compatibility scope, tests actually run and their results, and any untested
cases. Include the commands and a concise log/result reference. For hardware,
motion, heating, Canvas, camera, networking, storage or firmware changes,
identify the printer/base used and separate offline simulations from real
printer tests. If hardware testing is unavailable, mark the PR as draft or
explicitly request a tester; do not claim it is ready for release.

Maintain the existing guards, bounded memory/work, personal-data preservation
and recovery path. Explain changes to any of them. Add a regression test for
a reproducible defect where practical, and document why a required check was
not run. Record user-visible changes and original contributor credits in the
changelog. Documentation intended for release is in English.

Changes to firmware or native components with uncertain compatibility require
a new complete firmware base and a reviewed migration. Compatible panel-only
features continue through the normal application-update path. Declare the
supported base explicitly; do not distribute a native compatibility change as
a routine panel update merely because it fits the same archive format.

## Repository conventions

- Current files use stable paths. The CC2 Control version is written only in `cc2-control/VERSION`; release tags and the changelog name releases (see [`RELEASING.md`](RELEASING.md)), not working filenames.
- Generated firmware, binaries, credentials and private signing keys are never committed.
- Do not rewrite published history to tidy commit counts. Use a focused branch and squash it when merging if appropriate.
- Keep vendor material separate and record its origin and checksum.
- User-facing documentation is written in English; the UI may contain supported translations.

## Tests

```sh
cd cc2-control
make clean test CROSS= CC=gcc
```

These tests also run automatically on every pull request; a PR should be green
before it is merged. See [`docs/TESTING.md`](docs/TESTING.md) for firmware and
real-printer checks.
