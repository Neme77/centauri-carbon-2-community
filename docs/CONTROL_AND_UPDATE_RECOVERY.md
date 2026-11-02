# Control shortcuts and post-reflash update recovery

## Pressure Advance

The Control speed/flow card displays the native live Pressure Advance value and
offers explicit Apply and Disable actions. The supported runtime range is 0–1 s
with up to six decimal places. No smooth-time change, configuration save, heater
command or motion is implied. A print's start G-code may override this value.
Unavailable or stale native readback does not become a guessed configured value.

## Dashboard Quick Actions

The four configurable slots keep their existing defaults and saved selections.
Additional choices are Open flaps, Close flaps, Connections, Fans, Pressure
Advance, and bed heating/chamber preheat. Navigation shortcuts only open the
corresponding guarded controls. When the heating experiment is disabled its
shortcut opens Experimental integrations without enabling it.

Flap commands require fresh native Idle/complete state, both heater targets zero
and no bed-heating session. A shared in-flight lock covers both shortcuts and
the fan panel across navigation. Command acceptance is reported separately from
physical position; no position confirmation is invented. No load-cell shortcut
is provided.

## Updating after reinstalling the base firmware

A base reflash can restore native overlay hooks while retaining the installed
Connections component, settings and persistent DHCP entry under /opt/usr.
An otherwise valid update previously stopped with changed_startup and rolled
the app back. The installer recognizes only the complete supported native
post-reflash profile and reinstalls its delegates without deleting saved state.

Retained component checksums and native originals, the restored native hook
fingerprints, absent manager startup hooks and persistent DHCP identity must
agree. Mixed/foreign/symlinked inputs, active transactions and executing/mapped
old components are refused. Fresh cold Idle readiness is checked again before
commit, against the new app already installed by the normal updater. Existing
readiness checks are not weakened to accept an older schema.

Backups retain the previous component and exact startup files, including absent
files and original modes. Failure restores the pre-update state. A failed move
of the old component leaves that component in place rather than displacing it
during rollback. Installation does not restart native services or change the
live network. Reboot the complete idle printer after successful recovery.

## USB binding correction

A successfully bound RTL8153 interface is a child of its canonical physical USB
device, including behind hubs. Qualification checks that exact parent and
interface identity with the expected driver. The previously qualified revision
31fd remains available; revision 3000 still requires explicit experimental
consent and has not gained physical qualification. Kernel modules, ABI probes
and compatibility libraries are unchanged.

## Validation

The owner accepted the preceding Pressure Advance/31fd repair candidate.
The later recovery/Quick Actions candidate passed 44 actual-installer scenarios
on dash and BusyBox, host/ARM preference and control regressions, sanitizers,
a full ARM build and smoke, and installer/rollback checks. Actual browser tests
covered persistent choices, flap locks across navigation, refusal feedback,
navigation-only actions and responsive layout. Synthetic recovery tests do not
claim an additional physical base-firmware reflash.
