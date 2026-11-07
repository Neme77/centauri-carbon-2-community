Persistent CC2 Control data lives here at runtime.

Expected files:
- cc2-control.conf           created by first-run setup; chmod 600
- material-presets.json      seeded from /opt/inst/cc2-control/defaults if absent
- bed-plates.json            build-plate library (meshes and Z offsets), created on first save
- spools.json                spool inventory, created on first save
- print-queue.json           print queue (jobs, copies, policy), created on first save

Do not overwrite existing user files during a firmware update.
