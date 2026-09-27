# V4.2 builder — CC2 Control 1.1.30

This release builder is derived from the validated V4.1/R8 build chain and integrates **CC2 Control 1.1.30**.

Archive:

```text
CC2_BUILDER_V4_2_CC2_CONTROL_1.1.30_INIT_FIX_R2_RELEASE.zip
```

SHA-256:

```text
eab6bae24857d86ee8a3bc15c5002329f7b33f64464b3cfe912b9aa636237527
```

## Included changes

- unified Bed Levelling workflow
- saved Side A / Side B visibility
- screw corrections in microns
- guarded reference optimization
- Quick Actions
- global press-and-hold Emergency Stop
- compact Settings panels
- persistent Dark and Light themes
- complete CC2 Control 1.1.30 source archive
- retained V4.1/R8 persistent-storage startup fix
- firmware init is pinned to `/opt/inst/cc2-control/start.sh`

## Internal naming

The builder intentionally retains some `v4_1` internal directory and script names from the validated V4.1/R8 lineage.

The **release designation is V4.2**. Renaming the internal tested build chain was avoided for this release so that the functional delta remains limited to the validated integration update.

## Post-install behaviour

No additional manual reboot or power-off/power-on cycle is required. First-run
registration restarts CC2 Control itself and realigns MQTT, Canvas and snapshot
state while leaving printer services running.
