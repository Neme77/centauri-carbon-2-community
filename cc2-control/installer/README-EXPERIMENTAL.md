# Experimental trial: Canvas UDS and Quiet

These optional features are included in the single CC2 Control 1.2.21 base.
No update-channel switch or separate application variant is needed.
The cumulative package also includes the qualified R2 camera component for
users skipping 1.2.19 and does not modify the printer executable. Feature
switches need no full reboot; first activation of the camera fix does require
a complete printer reboot.

For this trial, if 1.2.21 is already installed, extract the ZIP and use
Install-Windows.cmd or install-cc2-control.sh: the panel correctly refuses a
same-version package. Do not advance the version for a trial. Install only
while connected, idle and cold with heating targets zero. Keep the exact
backup path reported by the installer.

In Settings â†’ Updates, enter the LAN code in Experimental features. Both choices
default to off on new installations and remain independent; existing saved
choices are retained. Try Canvas UDS first, checking slot states
and Orca synchronization; turn it off before trying Quiet separately. Stored
opt-in is distinct from actual source availability: read the displayed status.

Quiet opt-in only reveals the controls on Control. It does not change motors.
Before any movement, verify driver readback and limits following PR #179. The
slicer can override the initial 80 mm/s and 2000 mm/sÂ² limits; do not use G-code
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
