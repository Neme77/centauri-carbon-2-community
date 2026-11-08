# Long-press pop-ups on the Home screen

Optional native pop-ups for the CC2 Community V4.4.3 touchscreen:

- **Long-press the Nozzle card** to choose the installed nozzle. Nozzles come
  from the CC2 Control plate library. The installed one is marked and nozzles
  without a bed-mesh measurement are labelled; any nozzle can be chosen, as on
  the Plates page (the mesh belongs to the plate, the nozzle adds its Z
  correction).
- **Long-press the Build plate card** to choose the plate and bed mesh: one row
  per measurement (temperature and nozzle), the active one marked. If the mesh
  is not loaded in the printer yet, the pop-up asks before the restart that
  loading it needs.

The pop-ups use the same CC2 Control requests as its Plates page
(`GET /api/plates`, `POST /api/plates/nozzle/select`, `POST /api/plates/mount`)
on `127.0.0.1:8081`. A choice is sent only while the printer reports Idle. A tap
on Close or outside the panel closes it. Only long presses are handled; short
taps are left to the screen program.

## How it works

The pop-ups are LVGL objects created by the screen program itself, in its own
fonts and the CC2 Community palette, on LVGL's top layer. Nothing draws over
the screen from outside and no other process is involved.

`nui.c` is compiled for the printer and appended to an owner-supplied copy of
the screen program by `patch.py`:

- the payload is one segment at `0x01400000`, above everything the program
  maps, mapped by turning the program's `PT_NOTE` header (the ABI tag, unused
  at run time) into a `PT_LOAD`;
- the Home view's show entry in the program's view table points at
  `nui_home_show`, which calls the original function first and then attaches a
  long-press handler to the Nozzle and Build plate cards (found by their
  position and size; a one-second LVGL timer re-checks);
- every LVGL and C library function the payload calls is resolved at build
  time to the program's own code and PLT entries, read from the supplied
  program with `nm` and `objdump`.

Only the exact V4.4.3 screen program
(`db09fa37d378df259427aaac392a341282e08ceb26cd87603542c0dcbcbb91d1`) is
accepted, both by the build and by the printer installer. Other versions are
refused rather than guessed.

## Source and provenance boundary

No ELEGOO executable, image or font is included, and the vendor program is never
executed by the build or the tests. Supply your own copy of
`/opt/bin/ec-eeb001-gui` from a printer running CC2 Community V4.4.3. The build
output is derived from it and stays private, as for `experimental/native-display`.

## Build

Requires the `arm-linux-gnueabihf-` GCC and binutils (set `CROSS` for another
prefix):

```sh
python3 build.py --gui /private/ec-eeb001-gui --out /private/native-popups
```

The output directory gets the patched `ec-eeb001-gui`, `nui.elf`, `syms.ld`
and `manifest.json` with the input, output and payload SHA-256.

## Install, check and remove on the printer

Copy `manage.sh` and the build directory to the printer, then as root:

```sh
sh manage.sh status
sh manage.sh install /opt/usr/native-popups-build
sh manage.sh remove
```

`install` requires the stock V4.4.3 screen program, a build whose file matches
its manifest, and an Idle printer. It keeps a copy of the stock program in
`/opt/usr/cc2-native-popups/`, replaces `/opt/bin/ec-eeb001-gui` through a
verified temporary file and does not stop or restart anything. `remove` puts the
kept stock copy back the same way. Neither touches a screen program it does not
recognise. A complete reboot of the idle printer loads the change.

A firmware reinstall restores the stock screen program; run `install` again
afterwards.

## Validation and limits

```sh
python3 test_patch.py      # synthetic ARM executable, patched and run under qemu-arm
python3 test_nui.py        # pop-up logic on the host with a fake LVGL; ARM compile
python3 test_manage.py     # installer against a temporary root, dash and BusyBox
```

The tests use synthetic inputs only. Physical testing on a CC2 Combo (V4.4.3,
CC2 Control 1.2.26): both pop-ups open on a long press, list the saved nozzles
and plate measurements, and close; the patched program starts at boot.
Choosing nozzles switched between two measured nozzles and an unmeasured one
and back. Choosing a bed mesh sends the same requests as the Plates page; that
path is covered by the host tests but has not yet been exercised on the printer.
A Chamber (DragonBreath) pop-up needs a printer-side DragonBreath address and
is left for a follow-up change.
