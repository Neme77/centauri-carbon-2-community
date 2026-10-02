# Isolated, navigable UI preview

This development tool runs the existing CC2 Control interface on a computer,
using local, deterministic fixtures. It requires no printer, SSH session or
installation. The branch also includes a compact desktop layout proposal. It changes UI sources
and the normal UI build, without changing the printer backend or control logic.

## Start

From a checkout of the preview branch, with the Node.js version used by CI:

```sh
cd cc2-control/web-src
npm ci
npm run preview:ui
```

Open the localhost URL printed by Vite (usually `http://127.0.0.1:5173`).
Keep that terminal open; stop with Ctrl+C. Use the browser's device emulation to
review desktop, tablet, portrait and landscape sizes.

The simulation panel switches between printing, paused, idle and disconnected
states. It reloads the page while preserving its route. Reset selects printing
and restores speed/flow to 100%. Telemetry is fixed, so screenshots are
repeatable. Normal navigation, sidebar, language and theme controls remain
interactive. Preferences and tuning changes exist only in simulator memory;
restarting Vite resets them. Tabs share that memory. Browser-local UI preferences
still use the app's usual localStorage on the localhost origin.

Camera and file thumbnails are local SVG placeholders. File metadata, bed mesh,
Canvas and job objects are fixtures. Tuning and pause/resume/cancel modify only
fixtures. All other commands, console commands, uploads, deletion, print start,
setup and unsupported endpoints are blocked. This previews their layout, not
real hardware behaviour. Simulated memory/CPU figures are not measurements.

The demo mode has no API proxy, ignores `CC2_BACKEND`, and replaces the direct
camera URL only in the development transform. It refuses to start if camera
isolation no longer matches the source. `vite build --mode demo` is rejected;
normal `npm run build` produces the UI with the proposed layout changes. `npm run dev` remains
the separate, existing mode for connecting to a real backend.

## Browser checks and screenshots

Install the test browser once, then run:

```sh
npx playwright install chromium
npm run test:preview
```

On Linux CI use `npx playwright install --with-deps chromium`.
The test launches Vite and Chromium together, navigates every page at 1440×900,
1366×640, 1280×720, 1920×1080, 1024×768, 768×1024, 390×844,
740×390 and 1280×480, checks for horizontal overflow and JavaScript
errors, repeats the desktop pages with the sidebar collapsed at five sizes, checks the
compact desktop header and bounded camera height, changes all four
languages and light/dark themes, exercises scenario controls and a tuning reset,
rejects hardware actions,
and verifies that browser requests stay on the preview origin.

To retain screenshots (PowerShell):

```powershell
$env:CC2_SCREENSHOTS = "$PWD\preview-screenshots"
npm run test:preview
Remove-Item Env:CC2_SCREENSHOTS
```

POSIX shells: `CC2_SCREENSHOTS=/tmp/cc2-preview-screenshots npm run test:preview`.
The menu test detects the visible drawer toggle instead of assuming a phone
breakpoint: this covers portrait tablets and short landscape desktop windows too.

Screenshots are review outputs, not approved visual baselines. A passing test
cannot certify that spacing, alignment, wording or proportions look good. First
approve the desired screenshots; only then add pixel comparisons against that
baseline. These checks also do not replace real-printer validation.

## Desktop layout proposal

Page grids now use the available content width as the sidebar opens or closes.
Desktop thresholds retain practical column widths; touch layouts keep their
existing thresholds. The desktop header is 52 px high, cards and controls use
smaller spacing, the camera height is bounded, filenames are limited to two
lines with a full-name tooltip, and speed/flow share a compact dashboard row
with machine information. Control columns adapt between two and three columns.
Review every page with the sidebar open and closed before approving the design.
