# Contributing

Contributions, test reports and focused bug fixes are welcome.

## Before opening a change

1. Reproduce the issue against the current `main` branch.
2. Keep one logical change per pull request.
3. Add or update a regression test where practical.
4. Run the CC2 Control test suite and the relevant builder tests.
5. Document any behaviour, protocol or installation change.

## Repository conventions

- Current files use stable paths; version numbers belong in source constants, release tags and the changelog, not in working filenames.
- Generated firmware, binaries, credentials and private signing keys are never committed.
- Do not rewrite published history to tidy commit counts. Use a focused branch and squash it when merging if appropriate.
- Keep vendor material separate and record its origin and checksum.
- User-facing documentation is written in English; the UI may contain supported translations.

## Tests

```sh
cd cc2-control
make clean all CROSS= CC=gcc
python3 -m unittest discover -s tests -p 'test_*.py'
```

See [`docs/TESTING.md`](docs/TESTING.md) for firmware and real-printer checks.
