# Repository working rules

These rules apply to contributors and coding agents working in this repository.

## Canonical sources

- Treat `cc2-control/` and `builder/current/` as the canonical development
  sources.
- Use stable filenames and paths. Keep release history and release numbers in
  Git tags, GitHub Releases and `CHANGELOG.md`; do not add version-specific
  documents or release-pointer documents to `main`.
- Keep historical snapshots and release artifacts read-only unless a change is
  explicitly intended for that historical version.

## Repository layout

| Path                     | Purpose                                                                         |
| ------------------------ | ------------------------------------------------------------------------------- |
| `cc2-control/src/`       | CC2 Control C backend                                                           |
| `cc2-control/web-src/`   | Browser UI sources (Vite + Preact + Tailwind, TypeScript); see "Web UI changes" |
| `cc2-control/web/`       | Committed build of `web-src/` (`index.html`, `locales/`); never edited by hand  |
| `cc2-control/tests/`     | Host-side and integration tests                                                 |
| `builder/current/`       | Current firmware builder and integration logic                                  |
| `builder/current/tests/` | Host-side builder tests                                                         |
| `docs/`                  | Stable installation, build, architecture and testing documentation              |
| `CHANGELOG.md`           | User-visible release history                                                    |

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
storage, motion or firmware integration. This applies to AI-assisted changes too.

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
cd ../tests && python3 test_web_sync_static.py   # build and firmware overlay copy must match
```

- commit the rebuilt `web/index.html` and `web/locales/` with the sources (CI rebuilds it and fails
  on any difference) and keep the committed copy under
  `cc2-control/firmware-integration/overlay/.../web/` identical: after `npm run build`, copy
  `web/index.html` and `web/locales/*.json` over it (`test_web_sync_static.py`);
- to add a colour theme, append an entry to `THEMES` in `web-src/src/lib/i18n.ts` and a `:root[data-theme="<id>"]` block with every token to `web-src/src/index.css` (`test_themes_static.py` checks it); the backend stores any lowercase-hyphenated identifier, so it needs no change;
- inspect the complete relevant code path before editing;
- preserve language behaviour and persistent UI preferences;
- route every new user-visible string through `t()` or `tpl()` with an identifier
  (`<group>.<name>`, e.g. `files.upload_file`; `common.` when several pages share it,
  `state.` for the machine states the backend sends in English). `web-src/public/locales/en.json`
  holds the English text and is bundled into the page; add the id to **every** file in
  `web-src/public/locales/` (key parity across all languages is enforced; never edit
  `web/locales/`, the build overwrites it). `t()` only accepts ids from `en.json`, so a typo fails `npm run check`;
  reword the English text freely, the id does not change
  (`test_locale_coverage_static.py` and `test_locales_static.py` enforce the rest);
- register every periodic request with `poll()` or `usePoll()` from `web-src/src/lib/poll.ts` (never `setInterval`): the CC2 is resource-constrained, so the scheduler never overlaps runs of a source, sleeps in hidden tabs (except the OrcaSlicer pending-print check) and lets pages poll faster only while they are open (`test_polling_static.py`);
- test the actual browser control or event path that changed; a direct function call or
  synthetic unit test is not sufficient for interactive UI behaviour;
- the demo (`npm run preview:ui`, `preview/plugin.ts`) starts in **English**. Keep it that way,
  and give a `preview/test-*.mjs` the dictionary of the language it really displays (`en.json`
  before it switches language, `it.json` after). `preview/fixture.mjs` is a separate demo
  with its own language: check which one a test uses before changing either;
- the `preview/test-*.mjs` scripts need a Playwright browser (`npx playwright install chromium`);
  not all of them read `CC2_BROWSER_PATH`, and `--single-process` crashes a system Chrome, so
  without the Playwright browser run a copy of the script that passes `executablePath` and
  drops those flags;
- verify the page against a running CC2 Control instance when practical:
  `CC2_BACKEND=http://localhost:8099 npm run dev` serves the UI with hot reload
  and proxies `/api` and `/i18n` to that backend.

## Web UI design system

Reuse the shared pieces in `web-src/src/components/ui/` (`Page`, `Card`, `CardHead`, `Dialog`, `DialogTitle`, `Button`, `Select`, ...) instead of restating their classes in a page. A visual difference between two pages must be a decision, not a copy-paste drift; if a pattern is missing, add it to `components/ui/` and use it everywhere.

| Role                          | Use                                                                                                                                 | Never                                                                           |
| ----------------------------- | ----------------------------------------------------------------------------------------------------------------------------------- | ------------------------------------------------------------------------------- |
| Page title and subtitle       | `<Page title sub>` (`h2`, `text-2xl`). The dashboard deliberately has none                                                          | a hand-written page title; a title on the dashboard                             |
| Card                          | `<Card>` (`rounded-lg`, 12 px padding) with `<CardHead icon title end>` (`h2`, `text-base font-semibold`)                           | a card without `CardHead`, another heading size for a card title                |
| Sub-block title inside a card | `h3` with `text-base font-semibold`                                                                                                 | `text-lg`, `text-xl`, or an unsized bold `h3`                                   |
| Dialog title                  | `<DialogTitle>` (`text-xl font-semibold`); pass only the margin (`class="mb-4"`, `mb-1` before a subtitle, `my-2` under an eyebrow) | a raw `h2` in a dialog, `text-lg` or `text-2xl`                                 |
| Space between blocks or cards | `gap-3.5` (grids) or `mt-3.5` (a block that follows a grid; `Page` has no spacing between its children)                             | `gap-2.5`, `gap-3`, `mt-3` or `mt-4` between blocks                             |
| Space inside a card           | `gap-2` (controls), `gap-1.5` (tight rows), `gap-3` (form fields)                                                                   | `gap-4` and above except in a hero or a layout with a stated reason             |
| Radius                        | `rounded-lg` for cards and panels, `rounded-md` for buttons, fields and list rows, `rounded-full` for pills and dots                | `rounded-xl` outside dialogs, mixing `lg` and `md` on the same level            |
| Text                          | body text inherits its size from `index.css` (no class); `text-xs` for captions and secondary text                                  | new arbitrary sizes (`text-[13px]`, `text-[15px]` exist as legacy; add no more) |
| Colour                        | the theme tokens (`text-muted`, `text-cyan`, `border-edge`, `bg-panel`, `bg-field`, ...); every theme defines each token            | a hex colour for the interface (hex is fine for data, e.g. a filament colour)   |

Layout rules that were broken once, and why:

- A control must not move when a state toggles (menu expanded or collapsed, panel open or closed). Give the toggled element a fixed offset instead of centring it, and keep the space of what disappears (render it `invisible`, do not unmount it).
- A label must not wrap while a width animates: `whitespace-nowrap`, with `overflow-x-hidden` on the scrolling parent.
- A CSS rule on a container's children (`.cc2-topbar > div:last-child > div`) also hits any dialog rendered inside it, and a fixed-position backdrop with a forced `height` no longer covers the screen. Scope the rule (`:not([role="presentation"])`) or render the dialog elsewhere.
- Verify a layout fix by measuring (`getBoundingClientRect` in both states), not by eye: it found a 28 px shift that looked fine.

Accessibility baseline. Every page of the demo must pass axe with no violation, and there is no automated test for it yet (`axe-core` is not a dependency): run it by hand with `npm install --no-save axe-core` in `web-src/`, injecting `axe.min.js` into each page of the demo (all pages in `#cc2-navigation`, desktop and phone width, light and dark theme).

- every `select`, `input`, `progressbar` and icon-only button has an accessible name (`aria-label` from `t()`; `title` alone does not count);
- landmarks of the same role have different names (`aside` has `aria-label`, `nav` has its own);
- the focus ring is the global `:focus-visible` rule in `index.css`: do not remove outlines on a control;
- route changes move the focus to `<main>` (`lib/state.ts`): keep one `main`, with `tabIndex={-1}`.

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

## Working Principle: Root Cause First

- Every problem (bug, failing check, regression, review remark, tool error that keeps coming back) has a root cause. Find it with "5 whys" before fixing; a fix that only hides the symptom is not done.
- Then stop it from happening again, preferably with an automatic safeguard (test, type, lint rule, CI step) rather than a note. If that is not possible, record the cause and the rule in this file.
- A recurring warning or error is a problem, not noise: do not work around it again and again.
- When reporting a fix, state the root cause and what now prevents it. If a bug could not be reproduced, say so; never claim it is fixed.

## AI-assisted changes

AI-assisted work follows the same review, testing, safety and attribution rules
as any other contribution.

- Review generated or suggested code before committing it.
- Do not accept generated documentation that makes unsupported technical,
  licensing or provenance claims.
- Keep maintainers responsible for architecture decisions and release approval.
