# CC2 Control 1.2.20 — Experimental

**Prerelease for testers. The stable release remains 1.2.19.** Compatible with
Community Firmware V4.4/V4.4.1, ELEGOO base 02.01.01.07. Full-firmware and builder
downloads are unchanged.

This is one cumulative application package. In Settings → Updates choose the
experimental channel explicitly; the compatible newest application is offered,
not a list of installable modules. Channel selection never activates a feature.
The bell continues to report stable releases. On older stable interfaces this
channel is labelled experimental Canvas UDS. Users of the preceding stable-only
trial can upload the signed `.cc2update` from the panel once, then use the restored
selector. Returning to an older stable application requires the installer backup;
choosing Stable does not perform a downgrade.

## Changes since 1.2.19

- **Independent experimental choices:** Canvas UDS and Quiet controls are
  separately enabled, initially off for a new installation. Existing choices
  are retained. Canvas UDS preserves MQTT fallback; changing choices requires
  LAN confirmation and fresh connected idle/cold/zero-target state (#200, #206).
- **Canvas:** compact slot cards, shared actions, aligned settings and bounded
  session history. The explicit native recovery-load action for an unassigned
  head remains available; printer readback confirms recovery (#190, #207).
- **Object exclusion:** one inline 2D bed map with an expanded view, linked names,
  outlines and current-object highlight. Selection never changes the original
  G-code ID sent through the protected exclusion path (#199).
- **Spool insertion popup:** create an independent copy without moving the
  original spool, and calculate remaining filament from total minus tare while
  keeping nominal capacity separate. The Spools section is unchanged (#210).
- **Files:** reach files beyond the first 128 entries and stream uploads up to
  512 MiB, retaining bounded metadata windows, traversal limits, free-space checks
  and incomplete-transfer cleanup (#197).
- **UI and documentation:** dialog/sidebar fixes, keyboard focus, accessible
  names and shared components; a more concise homepage and documentation (#196,
  #201). Channel selection is translated into all nine supported UI languages.

## Qualification and experimental limits

The maintainer accepted Canvas UDS, correct slot states and Orca spool
synchronization on 23 October 2026 using the preceding integration trial.
Native/ARM and browser validation of this release is linked in
[integration PR #212](https://github.com/Neme77/centauri-carbon-2-community/pull/212).

**Quiet (#179) still needs V4.4 hardware qualification.** Enabling its controls
does not change motors, but selecting Quiet does. Slicer commands can override
the initial 80 mm/s and 2000 mm/s² limits. Normal restores fixed 500/20000 values;
displayed mode records a request, not verified TMC registers. Select Normal and
wait for success before disabling Quiet, changing Canvas source or updating the
application while Quiet controls are enabled. The original author's observations
remain credited separately from tests on the community base.

Extended pull-back (#172/#202/#209) remains a separate firmware experiment. The
application only recognizes the exact qualified extended executable; this
package installs no extended runtime and does not enable distances above the
stock limit on an unmodified printer. A separately installed extended runtime
requires its own qualified installation/rollback and effective configuration
at or below 350 mm before restoring the original executable.

## Camera and installation

The cumulative package includes the **unchanged qualified R2 camera fix**, also
for users who skipped 1.2.19. **A full idle-printer reboot is required when first
activating that camera fix.** Refreshing the page or restarting only CC2 Control
is insufficient. Recognized installed camera components are preserved; the
installer does not reboot the printer automatically.

Install only when connected, idle and cold, with nozzle/bed at most 50 °C and
heating targets zero. Close live views first. Keep the exact backup path printed
by the installer. Settings, material presets, plate/nozzle libraries and spools
are retained. Signed panel updates and the Windows/macOS/Linux installer are
attached, together with complete application and camera sources and checksums.

## Credits

Thanks to **[Surfoo](https://github.com/Surfoo)** for UI fixes and Canvas and
documentation ideas; **[BorjaLL](https://github.com/BorjaLL)** for Quiet;
**[Noahwhite2014](https://github.com/Noahwhite2014)** for extended pull-back;
**Nick Plateringen-Paul** for the original object-map idea/prototype; and
**[DamiBFryta](https://www.reddit.com/user/DamiBFryta/), our ultimate beta tester**, for
the clear suggestions and persistent testing. Thanks also to
**[tharje](https://github.com/tharje)** for the camera diagnosis/recovery work.
Original authorship, contribution history and earlier release credits are retained.

---

**Italiano:** prerelease sperimentale, la stabile resta 1.2.19. Selezionare il
canale Sperimentale; nelle vecchie interfacce è chiamato Canvas UDS sperimentale.
Nella precedente prova con solo canale stabile, caricare una volta il file
`.cc2update` firmato dal pannello. Canvas UDS e Quiet restano opzioni indipendenti,
inizialmente spente sulle nuove installazioni. Canvas UDS, stati degli slot e
sincronizzazione Orca sono stati confermati; Quiet su V4.4 e il runtime della
retrazione estesa richiedono qualifiche separate. Il pacchetto include il fix
webcam cumulativo: **alla prima attivazione serve un riavvio completo della
stampante inattiva**. Per istruzioni, limiti e ripristino leggere `UPDATE_CC2.it.md`.
