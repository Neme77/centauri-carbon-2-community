# Reproducing the managed update

The release app uses the committed `cc2-control/` tree with its managed-panel
overlay. This is the exact source from the physically tested package, not a
new feature change. Base upstream commit: 944f92602bce292d74b44559c9f7df507fdf79ca.

The qualified ARM build used the Cortex-A7 hard-float toolchain with
`-DCC2_RFID_NATIVE_SCAN=1 -DCC2_PANEL_ABI2=1`, `-mcpu=cortex-a7`,
`-mfpu=neon-vfpv4`, `-mfloat-abi=hard` and a static link. The build command was
`make BUILD=build-ARM CC=<qualified-arm-gcc> EXTRA_CFLAGS="-Werror -DCC2_RFID_NATIVE_SCAN=1 -DCC2_PANEL_ABI2=1 -static -mcpu=cortex-a7 -mfpu=neon-vfpv4 -mfloat-abi=hard" build-ARM/cc2-control`.

Sources for the signed sequence, manager admission, preparer and display
integration are supplied in the companion integration source archive. Generated
vendor binaries and private signing keys are not source files. Obtain restricted
vendor inputs separately and follow NOTICE.md. Public-key headers are verification
anchors, not signing secrets. New source builds require new artifact identities
and their own signature; do not substitute them into an existing signed package.

The tested app binary SHA-256 is
`6c805fe9c9d4beaa7c2c5354bf08660890be33979ff889bc5d22aba01acbed06`.
The immutable package SHA-256 is
`8f878c1c7dd3664fb94b6f75bc8362470b56382d63623cfc6f2c2618ad02c4cc`.
PRs 427, 429 and 432 are deliberately deferred and absent from this release.
