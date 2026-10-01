# Responsive page layout

Desktop devices with a fine primary pointer and hover support use the established
viewport breakpoints, preserving the page column counts from the UI before #44
and the UI after #44. Opening or collapsing the sidebar changes available card
widths without changing those viewport column thresholds.

Touch devices (coarse primary pointer or no hover) retain the content-sized grids
from the first #45 test. Their named CSS container measures main content after
sidebar margins and padding. The portrait drawer and short-landscape navigation
scrolling remain present. Movement step labels stay on one line.

The distinction uses CSS input capabilities, not operating-system or browser
user-agent detection. Hybrid devices follow their primary pointer capabilities;
real-device validation is still needed. No JavaScript resize listener or polling
source is added.

Optional browser verification against the built single-file UI:

```sh
CC2_PLAYWRIGHT_MODULE=/path/to/playwright/index.mjs \
CC2_BROWSER_PATH=/path/to/chromium \
node cc2-control/tests/test_responsive_layout_browser.mjs
```

The test serves the real built UI with mock API data, clicks every navigation
entry, opens/collapses the desktop sidebar and checks document/card containment.
Desktop Control keeps three columns at 1280 px and above regardless of sidebar
state. A touch tablet at 1366 px keeps the previous content-sized two columns.
Narrow portrait and short landscape navigation are covered. These checks do not
replace visual validation in OrcaSlicer or on physical desktop/mobile devices.
