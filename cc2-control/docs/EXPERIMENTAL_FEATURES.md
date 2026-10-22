# Optional experimental features

Application updates use the stable signed catalogue. Under Settings → Updates,
the separate Experimental features card provides independent, explicit opt-ins.
This is not a plugin store and no extra executable is downloaded by a switch.
Both features default to off. An old browser choice of the `canvas-uds` update
channel does not opt in: it is ignored for application update selection.

The choices are machine-level, stored in the private regular file
`/opt/usr/cc2-control-experiments.json`, outside the application installation.
Ordinary updates keep that file. Invalid or unreadable storage starts with both
features disabled and is not silently overwritten. Experimental choices are not
included in the portable settings backup: importing data on another printer must
not enable experiments. Downgrading to an older app that lacks these features
does not provide them; the saved choices are not a compatibility guarantee.

Every change requires the LAN code, explicit browser confirmation and the same
connected, fresh, idle/cold/zero-heating-target guard as installation. Settings
are written atomically; one choice never replaces the other. Failed writes do
not change the running source. No printer restart or motion/heating command is
sent by an opt-in switch.

## Canvas UDS

Enabling replaces the normal UDS subscription with the full Canvas subscription
from #200 by reconnecting the existing telemetry connection. Disabling restores
the normal subscription. Existing direct-state authority is cleared immediately
until fresh data arrives. A complete valid Canvas UDS cache supplies metadata
and Orca lanes; otherwise the untouched MQTT view is used. The page distinguishes
enabled from actually using UDS. Existing MQTT refreshes are not removed, and
this does not promise reduced RAM or CPU. The existing command-line opt-in stays
available for development and cannot be overridden from the page.

## Quiet motor controls

Enabling only reveals the Quiet/Normal controls from BorjaLL's #179. It does not
change chopper mode. The protected Quiet action is refused while opted out;
Normal remains available to restore the native mode. The original warnings and
confirmation remain: initial 80 mm/s and 2000 mm/s² limits can be overridden by
the slicer, so test G-code must respect those limits throughout. Normal restores
fixed 500/20000 values, not custom previous limits. The reported mode records a
request; it is not verified TMC register telemetry.

Before an application update, disabling these controls, or changing Canvas source while they are
enabled, the user must explicitly request Normal on the current UDS connection
and wait for its Console command to complete successfully. The settings endpoint
never sends this command itself. This prevents hiding controls or losing the
connection-bound mode tracker while a Quiet request remains active. Console
success still does not independently certify the physical motor state.

## Separate firmware experiments and cumulative releases

Raising pull-back above 350 mm (#172/#202) modifies the printer executable and
needs a separately qualified runtime install/rollback. It is not a settings
switch. Do not restore the original executable while its effective saved
configuration exceeds 350 mm.

The camera R2 fix is an ordinary cumulative-update component, not an experimental
opt-in. Public releases must use `installer/package.py --release --camera-runtime
QUALIFIED-RUNTIME-DIRECTORY ...`; `--release` refuses an omitted camera runtime.
The existing camera installer preserves a qualified installed component and
installs it for users skipping 1.2.19. A complete idle-printer reboot is required
when first installing the camera fix. CI application-only candidates are not
public cumulative releases.

## Validation

The integration has a C harness for independent persistence, corruption/refusal,
LAN confirmation, fresh/cold/idle guards, no implicit motor commands and
Normal-before-disable. Browser tests exercise actual switches, cancel/confirm,
backend errors, reload persistence, offline guards, stable-only update notices
and responsive layouts. Existing #200 and #179 tests remain; automated checks
are not new hardware qualification. Credits remain with the original authors.
