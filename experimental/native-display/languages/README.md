# Native touchscreen languages and graphics

Source candidate for a future CC2 Control release. This is the **original
touchscreen**, not the browser panel. Nothing here is automatically installed by
the panel updater or enabled in the firmware builder.

## Scope

- Keep the eleven native language indices and add Hungarian, Czech and Polish
  in Settings → Language (fourteen choices). The first-run wizard is unchanged.
- Persist the additional display selection separately in
  `/opt/usr/cc2-native-display-language`. Native daemon/error language indices
  remain bounded to their existing eleven values; extra locales use the English
  native index. This does not promise translated daemon-generated errors.
- Use a compact Hungarian Canvas hint that fits beside the illustration.
- Distinguish X/Y Home icons and add the chamber thermometer without replacing
  the surrounding theme background.
- Use the native `create_btn_center_img` helper and the active theme's
  `setting_cal_cal_back_bg.png` for Back, with native dimensions and label style.
  The previous private menu used an unstyled generic LVGL button, which inherited
  a different appearance. Its event fixture now exercises the actual Back callback.

No touchscreen network controls, polling timers, threads, Canvas transfers,
motion or heating commands are added. Existing button handlers retain their jobs.

## Provenance

Neme77 supplied the display catalogs, coordinated integration and tested the
languages/graphics-only private display on a printer. Hungarian corrections were
provided for native-speaker review with pezso92; review of this touchscreen kit is
still pending. Czech/Polish wording review is also welcome. Implementation and
test preparation were AI-assisted under Neme77's review.

Only authored source, supplied translation catalogs and SVG artwork are tracked.
No vendor executable, original theme image, font, extracted language table,
generated header, library or signing key belongs in this directory's Git history.
Retain the repository's vendor provenance/licensing boundary in NOTICE.md.

## Checks

On Linux, public fixtures need Python 3 and GCC, but no vendor input:

```sh
python3 experimental/native-display/languages/check.py
python3 experimental/native-display/languages/check.py --arm-cc /path/to/arm-linux-gnueabihf-gcc
```

The optional ARM check also needs `qemu-arm`. Fixtures execute real preference
and menu callback code with synthetic graphics objects. They cover all fourteen
choices, language persistence, invalid events, allocation/save failures and the
themed Back callback. They cannot establish visual correctness on an LCD.

Private exact-image checks require `pyelftools` and a locally extracted Universal
07 native display tree. `native_model.py` refuses any other executable hash:

```sh
export CC2_DISPLAY_INPUTS=/private/signed-display-inputs
python3 experimental/native-display/languages/prepare_runtime.py
python3 experimental/native-display/languages/test_runtime_layout.py
```

`prepare_runtime.py` generates ignored private headers and a dependency-patched
GUI. Native executable sections and original data/TLS remain unchanged on disk;
the constructor verifies the full executable and hook bytes before installing
four language hooks in memory. Unsupported inputs are refused.

Compile `language_core.c`, `display_language.c` and `runtime_boot.c` together as
the ARM shared library, using the generated headers and the repository's
`experimental/canvas-rfid/host/sha256.h`. Use the qualified printer toolchain,
`-fPIC -fvisibility=hidden -shared -Werror`, Cortex-A7 hard-float flags, and
`-Wl,-z,now,-z,relro,-z,noexecstack`. Verify the actual vendor loader dependencies
before packaging. Do not include any of the abandoned network prototype.

## Graphics and installation boundary

The two Home SVGs are new 46×46 artwork. `chamber-overlay.svg` is a transparent
68×234 overlay, to be composited over the locally supplied original
`ctrl_prepare_bg_mid.png` (SHA-256
`444c47dc28500aaf9c550548a16d50c39b5f78a5829b5892ef339b9960ee4569`).
Keep pixels outside the glyph unchanged. Apply resources to both the factory
default theme and its persistent cached copy; updating the factory copy alone
left the old background visible in the private trial. Other themes are not
silently overwritten.

An eventual installer must pin the base GUI, active app, native backend and every
resource; check idle state; back up the small replaced display component; write
the dependency library before the GUI; support verified rollback; and require
one manual idle-printer reboot. Use `/opt/usr` for staging, never printer `/tmp`.
This PR supplies source for integration, not permission to apply it to unknown
firmware or a ready-to-release 1.2.30 update.

## Validation record and remaining gates

- Prior languages/graphics-only component: Neme77 reports successful physical
  installation and operation. The tester kit for normal Universal 07 passed
  22 Bash/stock ARM BusyBox install/restore/refusal fixtures.
- This revision: catalog key/printf contracts and six exact-image ELF layout
  checks pass locally. Host callback results are recorded by the dedicated CI.
- The native-themed Back replacement needs a new ARM build and physical check:
  open Language, switch Hungarian/Czech/Polish, press Back, reopen the menu,
  reboot, and repeat with each supported active theme. Check small Canvas text,
  accents, X/Y labels, chamber background and normal CC2 Control operation.
- Before enabling in 1.2.30: qualify the cumulative installer against normal
  Universal 07 and the already-installed display variants, including rollback.
  Keep the PR draft until ARM/visual checks and the installation route are ready.
