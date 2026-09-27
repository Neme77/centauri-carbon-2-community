# V4.2 builder — CC2 Control 1.1.27

This release builder is derived from the validated V4.1/R8 build chain and integrates **CC2 Control 1.1.27**.

Archive:

```text
CC2_BUILDER_V4_2_CC2_CONTROL_1.1.27_RELEASE.zip
```

SHA-256:

```text
dd38b9aa4ecd779a246403aa3ba8c075f231d538b12694fa4d7303cdf01b71dd
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
- complete CC2 Control 1.1.27 source archive
- retained V4.1/R8 persistent-storage startup fix

## Internal naming

The builder intentionally retains some `v4_1` internal directory and script names from the validated V4.1/R8 lineage.

The **release designation is V4.2**. Renaming the internal tested build chain was avoided for this release so that the functional delta remains limited to the validated integration update.

## Post-install requirement

Every firmware installation produced for this release must be followed by a **full printer power-off / power-on cycle before any other operation** so Canvas and the background services can realign.
