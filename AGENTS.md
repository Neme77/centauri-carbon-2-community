# Repository working rules

- Treat `cc2-control/` and `builder/current/` as the canonical development
  sources.
- Use stable filenames and paths. Put release numbers in tags and the changelog.
- Keep changes small enough to review and pair behavioural changes with tests.
- Never commit LAN codes, local configuration, private keys, vendor firmware or
  generated build output.
- Preserve third-party notices and do not imply ownership of vendor components.
- Run the relevant tests before committing and record real-printer validation
  separately from host-side tests.
- Do not rewrite public history for cosmetic cleanup.

## What this repository is

Community firmware, local printer control (CC2 Control) and reproducible build
tooling for the ELEGOO Centauri Carbon 2, maintained by Neme77. Independent
project, not affiliated with ELEGOO. Firmware images ship a patched vendor
`elegoo_printer` binary plus this repository's own CC2 Control service; they
are not a from-scratch reimplementation (compare with OpenCentauri/COSMOS,
which targets the Centauri Carbon 1 with a full Yocto/Klipper rebuild — a
different project, different approach).

Start with [`README.md`](README.md) for the current status and
[`docs/ARCHITECTURE.md`](docs/ARCHITECTURE.md) /
[`docs/SOURCE.md`](docs/SOURCE.md) for how the pieces fit together. Do not
duplicate their content here; this file is about working effectively in the
repo, not restating the docs.

## Repository layout

| Path                                | Contents                                                                          |
| ----------------------------------- | --------------------------------------------------------------------------------- |
| `cc2-control/src/`                  | C backend (`main.c`, `mqtt.c`, `console.c`, `control.c`, `panda.c`)               |
| `cc2-control/web/index.html`        | The entire browser UI: one file, no build step, no external dependencies          |
| `cc2-control/tests/`                | Python drivers (some compile/run a native `.c` harness), see below                |
| `builder/current/`                  | Firmware assembly (`core/firmware_builder.py`), fail-closed against pinned hashes |
| `builder/v4.1-r8/`, `builder/v4.2/` | Historical reproducibility snapshots, read-only                                   |
| `docs/`                             | Installation, architecture, build, testing, source policy                         |
| `releases/`, `CHANGELOG.md`         | Version history; do not put version-specific facts elsewhere                      |

## Build and test CC2 Control

```sh
cd cc2-control
make clean all CROSS= CC=gcc          # host build, -Wall -Wextra -Wpedantic, must stay warning-free
python3 -m unittest discover -s tests -p 'test_*.py'
```

`unittest discover` currently reports 2 errors even on a clean checkout:
`test_panda_bridge.py` and `test_preferences_api.py` read `sys.argv[1]` at
module import time (the printer binary path), which `unittest`'s loader
doesn't supply. This is a pre-existing test-harness gap, not a build failure.
Run those two directly instead:

```sh
python3 tests/test_panda_bridge.py "$PWD/build/cc2-control"
python3 tests/test_preferences_api.py "$PWD/build/cc2-control"
```

Cross-compiling for the printer needs `arm-linux-gnueabihf-gcc` (`make clean
all`, no `CROSS=`/`CC=` override); the host build above is for fast iteration
and CI-style checks only.

## Firmware builder

`builder/current/core/firmware_builder.py` refuses to run when a required
input is missing or its SHA-256 doesn't match a pinned value — this is
intentional (fail closed), not a bug to work around. Restricted inputs (stock
firmware package, vendor printer reference binary, signing keys, prepared
SquashFS tools) are never committed; see `builder/current/README.md` for the
exact list. Tests that don't need those inputs run directly:

```sh
python3 -m unittest discover -s builder/current/tests -p 'test_*.py'
python3 builder/current/launch_helpers/tests/test_launcher.py
```

The stock signing key and AES key the builder validates against are ELEGOO's
own, published (apparently unintentionally) in their official
`elegooofficial/CentauriCarbon2` repository under
`elegoo/lib/signtools/key/`. This project does not hold or distribute a
private ELEGOO key; it consumes what ELEGOO already published. Treat that
provenance as a fact to preserve accurately in documentation, not to embellish
or understate.

## Editing `cc2-control/web/index.html`

Single 200+ KB file, vanilla JS, no framework. Translation today is one inline
`const italian={...}` object (English text -> Italian text) plus
`translatedText()`, which gates on a literal `currentLanguage!=='it'`/`==='it'`
check, and `applyLanguage()`, which walks every DOM text node with a
`TreeWalker` and looks up the trimmed text as an exact dictionary key. Before
changing anything in that path or the language `<select>`/onchange wiring,
read the whole relevant function first: hardcoded per-language special cases
have been added ad hoc over time, in inconsistent forms (different variable
names, different comparison styles), so a single grep pattern will not
reliably find every instance. `grep -c "==='it'"` (and variants) before and
after a change is a cheap sanity check, but it is not sufficient by itself —
see Verification below.

Many `notify(...)` toast calls still bypass translation entirely (plain
English string literals or template literals with `${error.message}`), a
known gap tracked for a future change, not something to silently "fix" as a
side effect of unrelated work.

## Verification discipline (learned the hard way)

Extracting the relevant `<script>` block from `index.html` and exercising it
under Node against the real running binary (`fetch` pointed at
`http://127.0.0.1:PORT`) is a fast, legitimate way to check a pure function
like `translatedText()` in isolation. It is **not sufficient** for anything
that goes through a DOM event handler (a `<select>` `onchange`, a button
`onclick`):
a harness that calls the extracted function directly, bypassing the actual
event path, can pass while the real page is still broken. Two bugs shipped
past exactly that gap in one session and were only caught by loading the page
in a real browser (Claude-in-Chrome tools) and driving the actual control —
native `<select>` dropdowns in particular need a real click sequence or
keyboard `Down`/`Return`, not just setting `.value` and dispatching a
synthetic `change` event, to match what a user's browser actually does.

Before calling a frontend change done: build the binary, run it, load the
page in a browser, and interact with the actual control that changed — not a
proxy for it.

## Attribution and history

Commit messages in this repository are a single summary line; do not add a
prose body describing rationale, testing performed, or a bulleted change list
— that belongs in the pull request description, not the commit. Never rewrite
already-published history for cosmetic cleanup (existing rule above); a local
branch not yet pushed/merged is fair game to reword or rebase.
