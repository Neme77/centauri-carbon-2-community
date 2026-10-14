# Canvas ejection trial on the current development base

This work preserves **sauadhi’s original implementation from PR103** and rebases its web/backend prototype onto develop after PR104. It does not merge PR103 or modify the published V4.4 firmware assets.

## Working prototype changes

- Existing Canvas reset, presence colours, seven languages, light recovery and the PR104 plate/calibration work are retained.
- Stop acknowledgement is followed by a **new valid status frame after the ACK**, with connection present and moving=false. An older, still-fresh stationary sample cannot confirm stop. The check is shared between the runtime and the regression tests.
- Host regressions cover old stationary frames, new moving-then-stopped frames, stale/disconnected feedback and failed acknowledgement.
- Web controls remain unavailable unless the optional runtime produces a valid active state; normal load/unload and reset remain separate actions.

## Explicit qualification boundary

The inherited native adapter supports **02.01.00.00 only**. The V4.4 builder now refuses its optional installation early and explicitly, before it can load or install that legacy component. **This branch is not an installable Canvas-ejection firmware for 02.01.01.07 yet.**

Offline analysis of privately held official 02.01.01.07 files found the Canvas functions inside the ET_EXEC printer executable. They are not exported in its dynamic symbol table. The old separate libelegoo_extras.so dependency and dlsym strategy therefore cannot be reused. The touchscreen addresses also differ. Candidate addresses and exact file fingerprints are recorded in [candidate-44.json](candidate-44.json); they are evidence for investigation, not validated motion bindings. No vendor binary is distributed.

Before enabling native motion:

1. Verify protocol connection, fault and filament-sensor semantics against the new implementation; matching an offset alone is insufficient.
2. Qualify the full GUI selection/button ABI and design optional runtime activation without weakening existing reactor checks, executable fingerprints or signing gates.
3. Build the adapters with the qualified GCC 6.5.0/glibc 2.23 toolchain and run ABI checks.
4. On an idle printer, verify all four slots, empty/nozzle-loaded rejection, actual reverse direction, inlet sensor clearance, cancellation from both interfaces and physical stop.

Keep this PR **draft and opt-in** until those native and printer checks pass. Automated host/browser tests do not establish motor direction or physical stop.
