# Object bed map

The Job page provides a **Bed map** button while live object data is available.
It opens the standard CC2 Control dialog with a top view of the 256 × 256 mm bed:
X increases to the right, and Y increases away from the front edge.

Numbered markers and the list use the same object order. Valid G-code polygons
supply optional decorative outlines; polygons are bounded to 512 points.
Objects without usable in-bed coordinates remain selectable in the list and are
never placed at invented positions. Coordinates may be native arrays or the
JSON strings returned by the printer's object status endpoint.

Cyan identifies the user's selection, amber the object currently printing, and
red an excluded object. The printing indicator never changes the user's selection.
Clicking a marker or list entry only selects it. The existing protected exclusion
command and confirmation use the exact original G-code ID, independently of labels,
copy numbering and map position. Disabled controls are not interpreted as exclusion.

The dialog shares the Job page's existing object data and polling lifecycle.
It adds no requests, periodic timer, backend dependency or printer-side renderer.
It closes on a job change or loss of live data. Escape, close button and keyboard
selection follow the existing dialog pattern, with focus returned to the opener.

Suggested and prototyped by **Nick Plateringen-Paul**. The integrated implementation
uses the object data model directly rather than scraping translated button text.

## Validation

`npm run test:bed-map` tests the production UI with simulated printer responses:
physical X/Y orientation, optional outlines, missing/out-of-range coordinates,
40 copies and bed corners, unchanged original IDs, selection across polling and
current-object changes, confirmation/cancellation, keyboard handling, close/focus,
light/dark themes, mobile bounds, and job/idle cleanup. Object names remain text.
Existing layout and object-exclusion browser regressions remain applicable.
Real-printer acceptance of this UI change is still required before release.
