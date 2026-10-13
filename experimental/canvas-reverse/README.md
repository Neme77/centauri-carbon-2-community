# CC2 manual reverse-feeder correction

Original package prepared 5 October 2026; extended for verified stock 02.01.00.00 on 10 October 2026. Offline development package. No printer has been modified and no physical feeder test has been performed. See `VALIDATION.md` for the new evidence and limits.

## What this fixes

The manual `CANVAS_MOTOR_CONTROL` handler compares absolute reported movement against a signed requested `DISTANCE`. A negative request therefore satisfies the stopping test immediately. This correction compares against the magnitude of the requested distance instead.

The source equivalent is:

```cpp
if (fabs(current_position - start_position) > fabs(static_cast<double>(dis)))
```

The binary correction replaces one 24-byte instruction block inside `libelegoo_extras.so`. It inserts absolute-value calculation for the target and removes a redundant conversion of an already-boolean 0/1 result. File length, function layout, the original strict greater-than threshold, conditional branch destinations and forward-distance behaviour are retained.

## Recognized original libraries

| Firmware OTA version | Support |
| --- | --- |
| `01.03.01.89` | Exact archived library recognized |
| `01.03.02.36` | Exact archived library recognized |
| `01.03.02.51` | Exact archived library recognized |
| `02.00.02.00` | Exact archived library recognized |
| `02.01.00.00` | Exact archived library recognized; new offset verified and 800 ARM comparison cases passed |

The 01.03.02.51 and 02.00.02.00 packages contain an identical CANVAS library. The 02.01.00.00 library differs and uses a separately verified offset. The tool checks the complete input SHA-256 and expected original instructions; version text alone is insufficient. Unknown, modified or incompatible input files are rejected. No modified vendor library or full firmware image is included in this ZIP.

## Contents

- `patch_library.py`: Python 3 standard-library tool; default operation checks only.
- `manifest.json`: exact original/patched checksums, offsets and instruction bytes.
- `reverse_distance_source.patch`: equivalent one-line source correction against ELEGOO commit `5a2ea7fc03e707552701b1a69f463699cbd39230`.
- `verification.json`: ARM instruction-emulation results for all four archived packages.
- `verify_arm.py`: optional reproduction of the comparison tests using your matching original library.
- `patcher_verification.json`: results of local patcher checks, including rejection of incompatible files and protection of original input.
- `validation.json`: new 02.01.00.00 compatibility, ARM and patcher verification results.
- `verify_extended.py`: reproduces the 800 comparison cases using the recognized 02.01.00.00 original library (requires the same optional Capstone/Unicorn dependencies as `verify_arm.py`).

The repository builder suite also runs `builder/current/tests/test_canvas_reverse.py`.
It checks the CLI with synthetic files and a temporary test-only manifest, so CI
needs no vendor binaries. Run it from the repository root with
`python3 -m unittest discover -s builder/current/tests -p 'test_canvas_reverse.py'`.
Actual ARM comparison reproduction still requires a recognized original library.

## Check a local backup

Copy the actual installed library from the printer using an established access method for that release. Its location in the inspected firmware images is `/opt/lib/libelegoo_extras.so`. Keep that original backup unchanged. The following commands operate on local files on your computer; they do not install anything on the printer.

```bash
python3 patch_library.py --input /path/to/backup/libelegoo_extras.so
```

If the checksum is recognized, create a separate output:

```bash
python3 patch_library.py --input /path/to/backup/libelegoo_extras.so --apply --output /path/to/backup/libelegoo_extras.fixed.so
```

An optional `--version 01.03.02.51` requires that exact supported version entry. Existing output files and attempts to overwrite the input are refused. Output bytes and checksum are verified before and after writing.

## Validation already completed

The actual original reverse-command failure was reproduced through ARM emulation. The corrected 44-byte comparison/branch block passed 80 cases per archived firmware, 320 cases total. Distances covered positive and negative 1, 10, 600 and 1400 mm; reported movement covered zero, below threshold, equal to threshold and above threshold, in both directions. Forward cases were compared against the original instruction block. Branch destinations were verified.

This validates the modified comparison, not the entire printer application, asynchronous controller communications, reported position fidelity, motor stopping distance, hardware sensors or full switching sequence. Emulation is not a physical printer test.

The added 02.01.00.00 library passed the original 80 cases and an expanded 800-case set, with the original bug reproduced, forward comparisons preserved and branch destinations verified. Its patcher checks also passed. Earlier JSON reports describe the original four-version package; `validation.json` records this extension.

## Deployment remains separate

This ZIP is not a `.zip.sig` firmware update and should not be presented to the printer's Offline Update menu. Deployment requires the exact installed version, a recognized backup, a verified access method, and a way to restore the original library. The printer's filesystem and startup behaviour must be checked before choosing a library override or firmware packaging route. No blind overwrite or flash command is supplied here.

After deployment is established, first test a short, slow reverse move on a free filament path under supervision with a bounded positive timeout. Verify physical movement and stopping, then test forward motion and the normal original switching arrangement. Complete the external-hub conversion only after that baseline works.

The patch does not alter existing zero-distance/zero-timeout behaviour, unload/load limits, channel mapping, cutter logic or tangle detection. Those remain part of the external-hub implementation work. In particular, the inspected manual command's `TIMEOUT=0` behaviour is not a reliable way to request continuous motion or disable timeout; it is outside this correction and should not be used for the proposed tests.

## Sources and attribution

- [ELEGOO CC2 source](https://github.com/elegooofficial/CentauriCarbon2), published under GPL-3.0.
- [Pinned source handler](https://github.com/elegooofficial/CentauriCarbon2/blob/5a2ea7fc03e707552701b1a69f463699cbd39230/elegoo/extras/mmu.cpp).
- [Archived firmware packages](https://docs.opencentauri.cc/software/updates-cc2/).
- [Source repository for archived packages](https://github.com/suchmememanyskill/cc2-firmwares).

This is an independent experimental correction, not an ELEGOO release. The source diff retains the upstream code's GPL-3.0 licensing. The Python tools and manifest in this package are supplied under the MIT license in `LICENSE-tools.txt`.
