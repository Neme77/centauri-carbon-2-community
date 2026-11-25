# Manual display-style kit

See [the user guide](../../docs/ORIGINAL_DISPLAY_STYLE.md). This directory contains
the exact tested printer-side installer, Windows/portable SSH launchers, mocked
launcher tests and a deterministic packager. Vendor binaries and PNGs belong in
the separate Release ZIP, never in Git. The original tested bundle is pinned;
packaging does not rebuild or alter it.

```sh
python3 -B -m unittest discover -s tools/display-style -p 'test_*.py'
python3 -B tools/display-style/package.py --bundle /path/to/display-kit.tar.gz --output /path/to/assets
```

Python packaging/tests need Python 3.9+. The bundle must match SHA-256
`034f420bfff43632367d2b5ca7cc919f730486675f9bd7bc8611ecc87d19f697`.
Packaging validates all 862 target payload hashes and compares installer source
against the physically tested archive. The ZIP excludes printer-stage.json,
passwords, local reports and backups. Retained physical/offline evidence is
summarised in qualification.json. Printer-side RFID evidence uses the unchanged
qualified helper from the published signed update.

ELEGOO retains ownership of native GUI/artwork. Language and preview fixes keep
the existing project credits. This package is a presentation variant, not an
alternate ownership claim or a new signed online application update.
