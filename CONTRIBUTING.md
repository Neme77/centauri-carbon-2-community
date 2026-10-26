# Contributing

Contributions, test reports and focused bug fixes are welcome.

## Before opening a change

1. Reproduce the issue against the current `main` branch.
2. Keep one logical change per pull request.
3. Add or update a regression test where practical.
4. Run the CC2 Control test suite and the relevant builder tests.
5. Document any behaviour, protocol or installation change.

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
