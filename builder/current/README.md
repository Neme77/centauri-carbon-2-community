# Firmware builder V4.4.2 — CC2 Control 1.2.21

Hardware-test candidate for the unchanged official ELEGOO base **02.01.01.07**.
The complete firmware is generated **on the maintainer's PC**, using local
official firmware and signing inputs. This source package is not a flashable image.

## Build on Windows / WSL

1. Extract the whole package to a local Windows drive. Keep `builder/`,
   `cc2-control/` and `experimental/` together.
2. Run `builder/current/Build-Release.cmd`. When requested, select the previous
   qualified builder's `builder/current` directory. The launcher copies only
   missing local keys, OpenSSH, SquashFS tools and the exact official firmware
   input; it does not copy an old generated image or old application sources.
3. Use the same Ubuntu WSL environment and qualified GCC 6.5.0/glibc 2.23 toolchain
   as the validated V4.4.1 builds. Other firmware bases are refused.
4. Keep the console open. The pipeline tests and prepares CC2 Control, extracts
   the pinned native libco from the authenticated official input locally,
   reproduces the reactor, checks the bundled R2 libraries, verifies signing
   inputs, then generates the firmware in `builder/current/output/`.

The firmware release number is `RELEASE_VERSION` (4.4.2); the compatible
application family remains `cc2-control/FIRMWARE_VERSION` (4.4). Application
version is `cc2-control/VERSION` (1.2.21). The experimental 1.2.20 app is separate.

## Integrated camera fix

R2 client-thread reaping and local/Matrix recovery are included as unchanged,
fingerprint-qualified libraries. At the original camera startup position, a
firmware launcher verifies the camera, startup and runtime, seeds the managed
component atomically on mounted persistent storage and starts it. Fresh installs
enable R2 on their first normal boot. Known prior updater-managed installations
are migrated while retaining their enabled/disabled choice and an exact backup.
Unknown, modified or incompatible state starts the native camera instead.
The vendor camera executable is supplied by the user's official firmware input;
it is not shipped in this builder. No cloud credentials are included.

The managed component remains compatible with subsequent cumulative application
updates. On an idle printer, `sh /opt/usr/cc2-control/camera/manage.sh status`
shows activation. `disable` takes effect on the next camera start; `rollback`
restores the qualified pre-camera startup and requires an idle-printer reboot.
The original Dual Trust, SSH, Z offset, camera coexistence and callback bridge
checks remain. Canvas UDS, Quiet and extended pull-back are not installed by this
stable integration.

## Validation boundary

Automated builder, startup, migration, rollback, native fallback and application
checks precede delivery. They do not validate the newly generated OTA on hardware.
After building locally, test first boot, camera in LAN and Matrix, restart,
Canvas, Orca upload, controls and a print before publishing a firmware image.
The preceding R2 camera patch has maintainer-reported seven-hour printing with
AI and streaming enabled; the new complete firmware still needs its own test.

Credit: **tharje** for camera diagnosis and evidence; **DamiBFryta**, our ultimate
beta tester, for persistent testing and feedback; all original contributors and
third-party notices are retained. See `../../docs/CONTRIBUTORS.md`.

## Italiano

Questo è il builder del **firmware completo V4.4.2**, con **CC2 Control 1.2.21**
e fix webcam R2 integrato. Il firmware va costruito sul tuo PC: avvia
`Build-Release.cmd` e indica la cartella `builder/current` del builder precedente.
Restano necessari la base ufficiale esatta 02.01.01.07, gli input locali e
l'ambiente WSL già utilizzato. Chiavi e firmware vendor non sono nel pacchetto.
Su una nuova installazione il fix viene attivato al primo avvio normale;
installazioni precedenti note mantengono la scelta attivo/disattivo. Se i
controlli falliscono parte la webcam originale. Il nuovo firmware completo deve
essere provato sulla stampante prima della pubblicazione.
