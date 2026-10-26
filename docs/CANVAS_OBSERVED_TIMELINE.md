# Canvas observed slot-state timeline (browser session only)

The Canvas page shows up to 24 recent state transitions derived exclusively
from the already-polled CC2 \`GET /api/canvas\` readback. Only transitions
between known \`empty\`, \`present\`, \`loaded\` and \`transition\` states
are listed. The first good readback establishes a baseline and creates no
event. Unknown, disconnected or failed telemetry resets the comparison
baseline, preventing spurious changes when connectivity returns.

Each timeline entry can also display the **most recent printer state** that
was available at observation time: idle, printing, paused or unknown. This
reuses the existing `/api/printer` cache with **no additional requests**;
a sample over five seconds old, unavailable, disconnected or from an
unrecognized printer state is always labelled **unknown**. The printer-state
sample is not synchronized to a physical filament action and does not prove
that a state transition happened during printing.

The timestamp is **when the browser observed the transition**, not when it
happened physically. The timeline does NOT observe actual filament movement,
purge-bin blockages, nozzle blobs, or successful completion of a mechanical
load/unload procedure.

Privacy/performance: events contain only numeric slot 0–3, fixed known
states, fixed printer-phase categories and local timestamps. No spool names, colours, filenames, G-code,
UIDs, authentication or network addresses are stored. No localStorage, file
writes, network uploads, additional polls, backend changes or extra threads.
The history is kept in ephemeral browser memory and cleared on page reload
or with the Clear history action. No auto-diagnosis or printer commands.

Before inclusion in an update, synchronise the rebuilt WebUI and all locale
mirrors with the firmware overlay and canonical builder source snapshot;
require full green CI and hardware acceptance. Existing tests for slot
action enablement and mobile layout are extended to cover the timeline.
