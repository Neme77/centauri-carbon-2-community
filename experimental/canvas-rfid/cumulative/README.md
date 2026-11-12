# Cumulative PC trial staging sources

These scripts are the source of the owner-tested closed-profile private bundle.
They are not a standalone installer. A qualified packager supplies the exact
`Native/` file table, ARM publisher, accepted EXHAUST payload and the complete
`Application/` installer. Never run them without that configured bundle.

`Run-Cumulative.ps1` verifies local hashes, stages the native component and runs
`prepare-native.sh` before the ordinary PC application installer. The native
preparation verifies all transferred hashes, restores execute permission only
on the verified regular publisher, checks/archives a completed old RFID trial,
and stages EXHAUST under the shared installer barrier. No native process is
restarted and no UART is opened in this step. The app coordinator performs the
subsequent guarded, reboot-separated RFID lifecycle.

`Test-Native-Preparation.py` executes 24 dash/BusyBox cases against inert
publisher/manager/retirement boundaries, including Windows-style mode0600,
symlink refusal, barrier ownership, ordering and bounded stale-only retries.
`Test-Cumulative-Launcher.ps1` runs six actual Windows launcher cases with inert
SSH/SCP functions. The complete bundle separately qualifies the actual publisher,
manager, installer, coordinator and native hooks; these boundary tests do not
replace those checks or physical acceptance.

The physical R1a cumulative trial was accepted by Neme77. Original-Canvas and
host restoration were previously accepted in the RFID-only autonomous trial;
retention of the EXHAUST baseline during cumulative restoration is covered by
the automated profile/coordinator checks and must not be described as another
physical restoration test. Public signed updater integration remains separate.
