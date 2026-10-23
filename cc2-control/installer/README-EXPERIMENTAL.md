# Experimental trial: Canvas UDS and Quiet

Application-only development package. This is not the stable release or
a signed automatic update. Extract the ZIP and use Install-Windows.cmd, or
install-cc2-control.sh on macOS/Linux, with the printer connected, idle and cold,
and heating targets zero. Keep the exact backup path reported by the installer.

This application-only package preserves an installed camera fix without
reinstalling it and does not modify the printer executable. The two switches do
not require a full printer reboot. Future public cumulative releases still
require the qualified camera component.

In Settings → Updates, enter the LAN code in Experimental features. Both choices
default to off and remain independent. Try Canvas UDS first, checking slot states
and Orca synchronization; turn it off before trying Quiet separately. Stored
opt-in is distinct from actual source availability: read the displayed status.

Quiet opt-in only reveals the controls on Control. It does not change motors.
Before any movement, verify driver readback and limits following PR #179. The
slicer can override the initial 80 mm/s and 2000 mm/s² limits; do not use G-code
that exceeds them. Normal restores fixed 500/20000 limits, not previous custom
values. Displayed mode is requested state, not verified hardware registers. This
combined application still needs printer acceptance; authors' hardware tests
and automated checks are recorded separately.

Before disabling Quiet, changing Canvas source or updating/restoring the app
while Quiet is enabled, explicitly select Normal in Control and wait for success.
Then disable the options. Restore the previous app using the exact backup path
printed by the installer, while idle and cold. Experimental preferences remain
outside the application; an older app cannot provide unsupported features.

Pull-back above 350 mm is a separate firmware experiment: application fingerprint
recognition is included, but no extended runtime is installed. Do not alter the
original firmware or increase distances to test these switches. An independently
installed extended runtime needs its own rollback, including effective settings
at 350 mm or below before restoring the original executable.

Credits: BorjaLL for Quiet (#179); Noahwhite2014 for extended pull-back
(#202/#209, issue #172); Surfoo for the Canvas layout idea; DamiBFryta for spool
popup suggestions and testing. Original contribution histories are retained.
