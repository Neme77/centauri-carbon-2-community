# Pending printer configuration changes

Canvas pull-back changes and plate meshes written to a printer slot apply after a
printer restart. Restart before saving other calibrations on the original screen:
the firmware's `SAVE_CONFIG` rebuilds its automatic configuration block from
memory and can discard a change written to the file after boot.

While an edit is pending, CC2 Control refuses PID calibration/saving, bed mesh
calibration, other plate edits and console commands until the pending edit is resolved.
Emergency stop remains available. The original printer screen operates
independently and can still save its configuration.

The panel records the requested pull-back separately from `autosave.cfg`. If a
native save overwrites it, the panel reports a conflict instead of claiming the
value is ready to apply. Save the desired value again, then restart. A changed
pending plate mesh is reported as unverified, and its automatic restart is
refused. Newer native settings, including Z calibrations, are retained; the panel
does not automatically rewrite them to resolve a conflict.

These guards and conflict reports address issue #267, reported by Noahwhite2014.
Automated reproduction uses fixture files and processes; printer validation is
recorded separately for the resulting release candidate.
