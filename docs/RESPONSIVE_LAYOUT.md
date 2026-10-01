# Responsive page layout

Page grids use a named CSS inline-size container on the main content area.
Column breakpoints therefore follow the available content width after sidebar
margins and padding, rather than the browser viewport. Sidebar and topbar
visibility still use viewport breakpoints; the portrait drawer is unchanged.

All eight pages and shared tuning/action panels use these content breakpoints.
Movement step labels remain on one line. Navigation can scroll in short
landscape windows while the desktop footer remains accessible.

Optional browser verification against the built single-file UI:

```sh
CC2_PLAYWRIGHT_MODULE=/path/to/playwright/index.mjs \
CC2_BROWSER_PATH=/path/to/chromium \
node cc2-control/tests/test_responsive_layout_browser.mjs
```

The test serves the real built UI with mock API data, clicks every navigation
entry, opens/collapses the desktop sidebar and checks content containment.
At 1440 px it checks that Control changes from two columns to three when the
sidebar is collapsed. It also checks narrow portrait and short landscape.
This does not replace visual validation in OrcaSlicer or on a real phone.
