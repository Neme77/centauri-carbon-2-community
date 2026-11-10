# 02.01.01.13 — maintainer gate checklist

How to turn the ported 0113 work into a signed, qualified release. Everything up
to the gate is done and host-tested; this is the part that needs the private
key, the vendor inputs, and real hardware — i.e. the maintainer. The build gate
(`require_buildable_profile`) refuses 0113 until `ported_patch_set: true` **and**
`02.01.01.13` is in `QUALIFIED_BASES`, so the gate flip and the hardware test are
deliberately coupled: you flip locally, build, test, and only merge the flip once
the test passes.

Never commit the private key, AES key, vendor OTA, `.so`, `.ko`, or any built
image. Profiles hold hashes only.

## 0. Local inputs (never committed)

- Private community key: `builder/current/keys/cc2_community_release_private.pem`
- AES key: `builder/current/keys/cc2_aes_key_v1.bin`
- Official stock OTA for 0113:
  `builder/current/original_firmware/cc2_eeb001_02.01.01.13_20261020214142.zip.sig`
- `mksquashfs`/`unsquashfs` 4.6.1 under `builder/current/tools/`
- Prepared runtimes, built locally for this base:
  - reactor `.so` — `python3 builder/current/prepare_reactor.py --base-version 02.01.01.13`
    (qualified GCC 6.5.0 / glibc 2.23 toolchain; see `REACTOR_0113_BUILD_AND_VALIDATE.md`)
  - camera — `python3 builder/current/prepare_camera.py --base-version 02.01.01.13`
  - connections runtime — `builder/current/components/connections/runtime.tar.gz`
    (the qualified archive; USB-Ethernet `.ko` are skipped for 0113 by the profile)

## 1. Prepare the gate flip (reviewable branch, NOT merged yet)

A small, reviewable change; keep it on its own branch and build from it locally.

1. `builder/current/base_profiles/02.01.01.13.json`: set `"ported_patch_set": true`
   (currently `false`). The profile already has no `TODO_` placeholders and all
   vendor/port pins filled, so nothing else in it changes.
2. Recompute the profile file hash **after** that edit and add it to
   `QUALIFIED_BASES` in `builder/current/core/firmware_builder.py`:
   ```python
   QUALIFIED_BASES={
    '02.01.01.07':'773acc75…',
    '02.01.01.13':'<sha256 of the edited 02.01.01.13.json>',
   }
   ```
   (`load_base_profile` pins the exact profile bytes for any qualified base.)
3. Update `BUILDER_SHA256` in `builder/current/launch_helpers/run_build.py` to the
   new `sha256(core/firmware_builder.py)` (the run-build wrapper refuses a builder
   whose hash it does not recognise).
4. Update the host tests that assert 0113 is *not* qualified:
   `builder/current/tests/test_base_profiles.py` (the `'02.01.01.13' not in
   QUALIFIED_BASES` assertion and the "refused until ported" build check flip to
   the qualified path).
5. `python3 -m unittest discover -s builder/current/tests -p 'test_*.py'` — green.

## 2. Build the signed image (maintainer, with the key)

Select 0113 as the base to build, then run the wrapper (community mode = an
update package; stock mode = an initial install):

```sh
echo 02.01.01.13 > builder/current/BASE_FIRMWARE        # or the file selected_base_version() reads
python3 builder/current/launch_helpers/run_build.py     # community signing, assembles all inputs
```

Equivalent direct call:

```sh
python3 builder/current/core/firmware_builder.py --base-version 02.01.01.13 \
  --signing-mode community --private-key builder/current/keys/cc2_community_release_private.pem \
  --aes-key builder/current/keys/cc2_aes_key_v1.bin \
  builder/current/original_firmware/cc2_eeb001_02.01.01.13_20261020214142.zip.sig \
  --mksquashfs builder/current/tools/squashfs-tools-4.6.1/bin/mksquashfs \
  --unsquashfs builder/current/tools/squashfs-tools-4.6.1/bin/unsquashfs \
  --connections-runtime builder/current/components/connections/runtime.tar.gz \
  --output builder/current/output/CC2_0113_COMMUNITY.zip.sig
```

Confirm the build's printed hashes match the profile pins before flashing —
printer `688b7bf8…`, GUI `668ab8cb…` (plain Z-offset; native display absent),
daemon dual `83433b14…`, camera/camera-init per profile. A key-only dry run:
`run_build.py` with `--check-signing-key-only` wiring (`build_arguments(..., check_key=True)`).

## 3. Hardware-test the builder-produced image (idle printer)

Flash the signed image through the normal update path and validate a full cycle:

- boots clean; UI, MQTT, camera, a real print, native Wi-Fi all work;
- reactor activates if included (`activation.marker`, `CC2_REACTOR_RUNTIME_V2=1`);
  the plain Z-offset GUI is present (native display absent);
- one idle → print → idle cycle with a stable printer PID;
- rollback is available (keep the stock OTA and the pre-update backup).

This is the end-to-end proof the **builder** produces a working image, not just
the hand-validated individual binaries.

## 4. Open the gate

Only after step 3 passes and you accept the result: merge the step-1 branch. 0113
is then a qualified, buildable base. USB-Ethernet and native display remain
not-included and are added later, independently (see
`USB_ETHERNET_0113_KERNEL_SOURCE_REQUEST.md` and `NATIVE_DISPLAY_0113_ANALYSIS.md`).

## Separate, not part of the gate

- CC2 Control compatibility for 0113 (`printer_sha256` → `688b7bf8`, multi-base):
  its own open PR; merge independently.
- Reactor formal 2b sign-off: one real print cycle on the device (the active
  validation already passed).
