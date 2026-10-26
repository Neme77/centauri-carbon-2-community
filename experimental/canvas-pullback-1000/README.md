# Canvas pull-back above 350 mm — experimental preparation

Dedicated integration branch: `experimental/canvas-pullback-1000`.
Original proposal, constant analysis and reported 560 mm printer tests:
**Noahwhite2014**, [issue #172](https://github.com/Neme77/centauri-carbon-2-community/issues/172).

**Not an installable update. The experimental catalogue is not activated.**
This branch initially supplies an offline, reversible reproduction of the
two-byte change, not a firmware release or a live installer. Stable `develop`,
the published application version and stable catalog remain unchanged.

## Scope and contribution split

- One opt-in experiment for extending `unload_fila_dist` from its existing
  60–350 mm bounds to 60–1000 mm on the exact supported program.
- CC2 Control contribution PRs can target this branch. The contributor's
  proposed background fingerprint check must verify the running executable,
  fall back to 350 on uncertainty, and revalidate eligibility before writes.
  PID reuse, firmware changes and in-flight reads must not retain an earlier
  extended-range authorization. No expensive hashing in the HTTP event loop.
- Runtime/builder integration and installer compatibility are separate pending
  work. Existing callback/reactor and camera patches must remain intact.
- Full filament ejection (#103/#114) and the design proposal (#187) are excluded.

## Offline reproduction

Use only the qualified V4.4/V4.4.1 community executable, not the unpatched
ELEGOO input. No vendor binary is supplied or published.

```sh
python3 patch_pullback.py PRIVATE_COMMUNITY_EXECUTABLE NEW_OFFLINE_OUTPUT
python3 patch_pullback.py NEW_OFFLINE_OUTPUT RESTORED_OFFLINE_OUTPUT --reverse
python3 -m unittest discover -s experimental/canvas-pullback-1000 -p 'test_*.py'
```

`--reverse` reproduces original bytes offline; it is **not** a safe live
uninstaller. The program original hash is
`19712b5943b10bb8cb6aab9a4ed498ee962b28538b23c91b7ed003eb9e590745`;
extended hash is
`15ea20771b1424560bfa0c6e4170993e1ce20001b90715d2b00f57dfcb0ea1bc`.
Only file bytes `0x5bba3d` and `0x5bba3e` differ. Whole input/output hashes,
input size, literal bytes and exclusive output creation are checked.

Maintainer offline validation reproduced both hashes, the two-byte difference,
exact reverse, wrong-input refusal and no-overwrite. No printer was modified.
Public CI exercises refusals and constant encoding; positive private-input
tests require `CC2_172_PRIVATE_INPUT` and otherwise explicitly skip.

## Gates before a tester package or catalogue entry

1. Integrate and review the contributor's application change, rebuilt UI,
   all translations and source pins. Show actual supported range and warn
   that original firmware cannot start with a saved value above 350.
2. Implement transactional runtime installation using exact fingerprints.
   Do not replace the active executable in place or relax signature checks.
   Verify startup, reactor preparation, preflight and all compatibility paths.
3. Implement safe removal: restore/validate configuration <=350 before the
   original program can boot, including main config and autosave overrides.
   A configuration mismatch must stop removal before executable rollback.
4. Qualify native/ARM/UI tests, original-program refusals, failed installation,
   restart persistence and exact rollback without changes to stable inputs.
5. Perform controlled printer tests of pull-back, reload, timeout and normal
   multicolour swaps. The contributor's 560 mm report does not qualify 1000 mm.
6. Select a compatible version/channel scheme before signing any catalogue.
   Current clients expose `stable`/`canvas-uds`, choose the first compatible
   release, and reject equal/older application versions. Do not silently turn
   the old Canvas-UDS channel into a different hardware modification.
7. Publish Italian/English installation, reboot and restoration notes, explicit
   experimental consent and original contributor credits.

No download should be offered until these gates pass. First distribution is a
separate controlled tester package; a signed channel entry comes afterwards.
