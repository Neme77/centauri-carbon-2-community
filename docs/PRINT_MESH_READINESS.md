# Print dialog mesh readiness

Original contribution: **[pezso92](https://github.com/pezso92)**,
[PR #150](https://github.com/Neme77/centauri-carbon-2-community/pull/150).

The print dialog distinguishes a saved side profile, confirmed absence and
unknown status. HTTP failures, missing/invalid profile collections and malformed
side entries are unknown. Side A (`default`) and B (`default1`) are classified
independently. A profile entry must be an object, not null, a scalar or an array;
this is an availability indication, not validation of its calibration matrix.
Existing backend validation remains authoritative.

Unknown status never implicitly enables calibration. The operator can either
choose calibration explicitly or request one read-only Refresh. Print submission
is inhibited during a refresh and while the chosen side is unknown without an
explicit calibration choice. A successful refresh retains that choice. A
confirmed missing side keeps the existing mandatory-calibration behavior.

The implementation adds no polling, service or printer connection, and sends no
motion or calibration command when opening or refreshing the dialog. Existing
Canvas mapping, print confirmation and backend checks remain unchanged.

Browser regressions exercise 503/missing-root replies, saved/missing A and B,
malformed collections and side entries, independent-side selection, retained
operator intent and the absence of print requests from read-only rechecks.

Release remains **1.2.16**: the prior candidate was not published. The updated
package also retains the accepted backup/restore, saved-language startup,
Hungarian/preferences fixes, alphabetic language menu and Console monitor.
Full CI and a new physical print-dialog trial are required before publication.
