# Changelog

All notable user-visible changes to this project are documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/). Releases are named after the CC2 Control version and, when the complete firmware changes, the Community Firmware version. Signed artifacts and exact checksums are attached to each [GitHub release](https://github.com/Neme77/centauri-carbon-2-community/releases). Installation guides: [English](docs/UPDATE_CC2.md), [Italiano](docs/UPDATE_CC2.it.md). Contributor credits: [docs/CONTRIBUTORS.md](docs/CONTRIBUTORS.md).

## [Unreleased]

### Added

- Two new colour themes, **Ember** (dark: warm charcoal with a glowing copper accent) and **Rose** (light: pale rose paper with a deep raspberry accent). Every text colour is at least 4.5:1 on its backgrounds. Requested and tested by **Neme77**, implemented with Claude Code (#453).

- Printer report card shows ELEGOO's text for 35 known error codes, with a link to the matching wiki.elegoo.com page, when the printer sends no message (#445, **efiten**; Hungarian review by **pezso92**).
- Add a manual Spoolman synchronization button with in-progress status and fresh spool imports, using the existing usage safeguards. Implemented by **Neme77**, requested by **HeiderakCZ** (#451; request #450).

- Show the Spoolman spool ID in spool choices, inventory, loaded Canvas/external slots and remote-catalog choices; search by ID or #ID and keep identically named linked spools individually selectable. Implemented by **Neme77**, requested by **onezero1010101** (#439; request #430).
- Spoolman card on the Spools page: one line under the fields tells what the last sync did in plain words, with what to do next: the time of the last sync, not synchronised yet, an address that is not valid, a login Spoolman refused, Spoolman not reachable or not answering in time, an incomplete reply, too much data, an address Spoolman does not know, an error from Spoolman, spools in Spoolman without a weight that were not added, or a full spool library. The texts are in all eleven languages; the translations other than English and German were made by the contributor and are not checked by native speakers. Implemented by **lpgamerzockt-ops** (#433).

### Changed

- The start page and the translation files can be sent gzip-compressed: when it installs CC2 Control, the installer writes a gzip copy next to each file with the printer's `gzip`, and CC2 Control sends the copy (with `Content-Encoding: gzip`) to a browser that accepts gzip while the copy unpacks to exactly the installed file, otherwise the file as before. The start page then needs about a third of its bytes, a translation about a quarter. The package and the update verifier are unchanged; without `gzip` or with too little space the plain files are sent. Implemented by **lpgamerzockt-ops** (#449).
- Spoolman: the refusals of the Spoolman functions (list of unsent usage, Spoolman settings, spool choice) now carry a stable `code` next to the unchanged English `error`, and the page shows them in its own language; a code it does not know falls back to the English text. The Spools page asks `GET /api/spoolman` once for all of its parts, and the list of unsent usage asks the printer again for Spoolman's spools at most three more times, counting every try. The page now shows the IPv4-only note under the address field, and a new address or login starts the next sync at once instead of up to two minutes later. Codes are listed in `cc2-control/docs/API.md`. Implemented by **lpgamerzockt-ops** (#433).

### Fixed

- Plate-library action menus attach their outside-click and scroll guards before becoming visible, preventing an immediately scrolled menu from remaining open. Added a deterministic browser regression and Ember/Rose preference persistence checks. Implemented by **Neme77** (#453).

- Connections: recognise the exact .13 manager already shipped in Community 4.4.4 / Universal 07, retaining the existing HTTP and file-integrity guards. Add a manual DHCP-delegate repair restricted to the verified native-script mismatch; aligned supported installations are left unchanged. Implemented by **Neme77** (#444).

- Manual preheat (`preheat:<nozzle>:<bed>`) is refused when text follows the bed temperature, as the temperature targets already were; it was accepted and the text ignored. A whole number outside the range of a 32-bit integer in a JSON the service reads (queue, plates, spools, history, Canvas status) is read as missing, instead of being cut to a wrong number. Designed and implemented by **lpgamerzockt-ops** (#435).
- Spoolman sync: a spool whose Spoolman entry is larger than 8 KiB (a long comment or many extra fields) is read and sent again, up to the size of one page of the list; it was refused in every cycle and its grams never reached Spoolman. A chunked reply too large to read counts as too large, so the list asks for smaller pages instead of stopping; a reply is complete once its Content-Length has arrived, also when a proxy keeps the connection open; results of a sync wait for the next round when there is no memory to take them over, instead of being lost; and the base path of the Spoolman address accepts only URL characters (no spaces, control characters, `?` or `#`) and the port only digits. Designed and implemented by **lpgamerzockt-ops** (#448).

## [CC2 Control 1.2.30] - 2026-11-25

### Release integration

- Qualified the cumulative signed 1.2.30 bridge on recognised 4.4.4, 07 and 07 HU bases with real ARM procd/ubus service sequences offline; the final package also passed a physical install/reboot/full-rollback/reboot cycle on Neme77's printer with RFID file hashes unchanged. Integration and printer validation: **Neme77**. The native display includes the language binding and Back touch-through corrections; original thumbnail implementation: **beanbo** (#411), .13 adaptation: **Neme77** (#412). Detailed author credits remain with each PR below and in `docs/CONTRIBUTORS.md`.

### Added

- After a bed mesh calibration replaces a saved measurement, the previous mesh is kept. Bed Levelling opens the change (new mesh minus the previous one) in the 3D view, the heat map and the value table: blue is lower than before, amber is higher, and the range is how the plate shape changed. The same view stays available on that measurement until the next change. Designed and implemented by **beanbo** (#418).

- Settings → Integrations has an experimental guide for ElegooSlicer: a copy of the CC2 printer profile with five print-host entries makes the slicer's Device tab show CC2 Control and sends Print to CC2 Control, where the Canvas spool is confirmed before the print starts. The guide shows the configuration folder, the lines to add with the address of the page, a button that copies them and the warning that the slicer falls back to the default printer profile; it is also written down in docs/ELEGOOSLICER.md. Implemented by **lpgamerzockt-ops** (#407).

- Native touchscreen source candidate: Hungarian, Czech and Polish in a fourteen-language selector, persistent display preference, X/Y Home labels, chamber-temperature glyph and a themed Back control. Prepared and printer-tested in its earlier languages/graphics revision by **Neme77**; the final integrated package subsequently passed the maintainer's native-display checks after installation and reboot, including the Back touch-through fix (#379/#383/#412).

- Bed alignment with a mesh on the Bed alignment tab of Bed Levelling: the four screw points are evaluated together with a saved mesh of the plate library. A plane is fitted through every point of the mesh, and what remains (heat map, range, RMS and a name such as dome or saddle) is the plate shape, which the screws cannot change. The shape at each screw is taken off the four probes, and the tilt in X and Y over 240 mm and the turn of each screw are calculated from what is left. A plate shape above 0.3 mm shows a note under the evaluation and never blocks it. The evaluation only reads; nothing is sent to the printer until a measurement is started. Designed and implemented by **lpgamerzockt-ops** (#348).
- The evaluation with a saved mesh on the Bed alignment tab replaces the Quick check of the Mesh alignment tab: with “With mesh” ticked and a saved mesh chosen, the four screw points are measured, quickly or in a session, and evaluated with the plate shape of that mesh, without measuring a new mesh. A mesh is offered at every age; one older than 30 days or with more than 10 prints since its measurement carries a note that recommends a new mesh. The page warns when the mesh was measured at another bed temperature than the screw points, belongs to another plate than the mounted one, was measured with another nozzle than the selected one, or the four probes differ from its plate shape by a twist above 0.05 mm. The limits are estimates. Designed and implemented by **lpgamerzockt-ops** (#348).
- Bed alignment on Bed Levelling: one tab replaces the Screw Levelling and Mesh alignment tabs (`#bed/screws` and `#bed/align` open it); the analysis of the side slots of the earlier tab and its verdict “Aligned” with a target tilt are gone; with “With mesh” the basis is a saved mesh of the plate library, the temporary mesh or the printer's active mesh (offered once when it is the same as a saved mesh; its bed temperature is chosen as without a mesh, and the warnings about plate, temperature, nozzle and age are left out). The firmware unloads its active mesh during a screw measurement (measured on a CC2: from the nozzle cleaning on, `GET /api/mesh` reports an empty `profile_name` and `probed_matrix`; the stored profiles stay), so with the active mesh as the basis the page keeps a copy of it from the start of each measurement in the browser and evaluates with that copy, also after another page or a reload and in a session; the next measurement takes the mesh the printer applies then. Step 1 chooses how to measure, quickly (four corners, or one corner again) or with a session, and beside it the tick “With mesh”, which works with both ways and is kept in the browser: with the tick the basis is a saved mesh or the temporary mesh and the evaluation takes its plate shape off, and “Measure a new mesh” is offered; without it the basis is only a bed temperature, from a material profile or typed in, and nothing about a mesh is shown. A way chosen earlier as “With mesh” opens as quick with the tick. The hold time of step 1, the same setting as in the Mesh tab, goes before every start the user makes outside a session (`screws:soak:<minutes>:` in front of `screws:measure…` and `screws:tram:start…`, nothing in front at 0 minutes), the question of the start names it, and the hold stands in the field at the top with “Stop” (`screws:cancel`); while it runs, the other screw measurements, a bed mesh calibration and the starts of the other printer sessions are refused (409). Step 2 shows the measured heights, the plate shape of the mesh, the turn of each screw and its deviation. The turn is given as clock face (in minutes or in hours), fractions of a turn, degrees or clicks of the screw tool, in the mode Auto (a turn that can be set by hand, with the rest named) or Exact. The turns use the CC2's own M4 screw (0.7 mm per turn, clockwise seen from below raises the corner) also without an experimental setting, and the experimental “Mesh alignment settings” are now “Screw pitch and alignment settings” (still `mesh-align-settings`): the target tilt, the maximum rounds, the turning direction and the two source fields are gone, and the screw pitch (0.3 to 1.5 mm per turn) can be set besides the tilt span, the shape limits, the screw positions and the row order. A pitch, a turning direction or their sources stored in the browser by the earlier version are no longer read, while its other stored values stay. Screw positions that the earlier version stored with its old defaults (30/30, 230/30, 230/225, 30/225) are read as the new defaults, the screw centres; positions set by the user stay as they are. With a mesh the top view switches between the screws, the 3D and 2D views of the Mesh tab and a plate map in µm, also in a session, and the tilt stands in two tiles. Info hints (i) explain the less obvious choices, a help text explains the modes and warns that the sleeve must not turn with the screw and that a screw must not sit against its stop, and when a corner the page gave a turn for has hardly moved by the next measurement, its row says to check whether the sleeve turns with the screw; a note says when the rear and the front corners differ by more than 1 mm, which usually comes from a bed carrier that hangs askew on its Z axes. The Mesh tab warns when the saved mesh it shows is older than the last screw measurement. The earlier sessions and quick measurements stand in a table. A mesh calibration, the alignment with a new mesh, the hold time and a screw measurement outside a session share one field at the top of every page of CC2 Control. By **lpgamerzockt-ops** (#403).
- “Measure a new mesh” (with “With mesh” ticked) measures one new mesh for a plate chosen in the question (the plate of the chosen mesh preselected) only as a temporary mesh, which CC2 Control keeps in memory and which leaves the plate's stored measurement as it was (`/api/plates/calibrate` with the line `work`). Then it starts the way of measuring chosen in step 1 by itself, the quick measurement of the four screws or the start of a screw session, without a hold time, also while another page of CC2 Control is open and after a reload; while the printer is still busy the start is tried again, and when it cannot be started a note with a start button stays. Directly after the mesh the printer service measures the screws without cleaning the nozzle again, and the line of a quick measurement at the top says so. After turning, only the screws are measured again. In a session the plate shape is taken off at the start, after a corner measured again and after “Measure again”, as in the quick measurement. As long as the latest measurement has every corner within the tolerance of 20 µm, the page asks whether to measure the final mesh, which replaces the stored measurement and ends an open session; the question stays after another page or a reload until the final mesh is measured. Until then the mesh shown for the new screw state is calculated from the screw measurement and marked as not measured, the page says that this mesh is only temporary, and once a later screw measurement shows that the screws were turned it warns that the stored mesh no longer fits the bed. When the mesh of a calibration cannot be read or stored at once after the probing, CC2 Control tries again every 2 s for up to a minute; if it still fails for a plate, a note at the top names the reason and offers “Save the mesh to the plate”. This replaces the guided loop of #348, which measured a full mesh in every round and was started through `/api/screws/align`; that service and its endpoints are removed. By **lpgamerzockt-ops** (#403).
- The four screw points are measured over the centres of the four bed screws, laid out as one rectangle (X 27.9 and 227.15 mm, Y 25.95 and 225.5 mm, measured on a CC2), instead of 30/30, 230/30, 230/225 and 30/225, which lay 2 to 5 mm beside them. The history marks sessions measured on the earlier points, because their heights are not directly comparable. By **lpgamerzockt-ops** (#403).
- A running screw measurement can be cancelled: the quick measurements and the measurements of a session are sent to the printer line by line, “Cancel measurement” (`screws:cancel`) stops after the line that is running (heating and nozzle cleaning finish first), lifts the nozzle only when X, Y and Z are homed (known from the printer's report when the job started and from the homing lines the job has completed; the lift is left out otherwise and after an error report, as the printer service accepted it with unhomed axes) and restores the saved state, and `GET /api/console` reports `cancellable`, `cancelling` and `cancelled`. Cancelling a quick measurement or the start of a session also switches both heaters off and ends a session whose start was cancelled; cancelling a later measurement of a session leaves the session running with its bed target. The field at the top of every page of CC2 Control shows a running measurement outside a session, as it does for a mesh calibration; a measurement of a session stands in the bar of the session. By **lpgamerzockt-ops** (#403).
- A printer error during a screw measurement (an error answer, or an error report even when its request was acknowledged) fails the measurement instead of counting as a completed one: it is not recorded in the history, the error text stays in the console output, the lines after the error and any cleanup movement are not sent, a session ends as a failed job (`end_reason` `console_error`) and a quick measurement leaves both heaters to the deferred shutdown. An error report that is torn across two reads of the printer connection (an acknowledgement followed by the beginning of the report) is recognised as well, before the next line is sent and in the last read of the job (a report that arrives in a read of its own after the result still costs the line that was already sent), and the Bed alignment page says that the printer reported an error and points to the Console page. When the optimized plan is chosen but this measurement does not allow it, a line under the strategy label says that it is not applicable and that the table shows the standard plan (the choice itself is not switched back). A report that was begun and never finished (the connection closes, goes quiet or runs out of time inside it, or a read fails) fails the measurement too, with its own message, “Console connection ended or command timed out”, and the text received so far kept once in the console output; it is neither a completed measurement nor a printer error, nothing is recorded and nothing is sent after it. A closed connection with nothing begun after the last result is still the end of the job. A cancel whose cancel tail is cut short the same way, or whose heater line (`M104 S0`, `M140 S…`) the printer refuses with an error answer or an error report, has not switched the heaters off (the rest of the tail is still sent): the deferred shutdown stays pending, and a session whose measurement was cancelled ends as a console error instead of going on. The same holds for an error report that trails the last heater line of the tail, and for a cancel that is pressed while the running line fails in the same moment (the job then ends as a failure without a cancel tail, so it is no cancel). A cancel and a lost connection stay separate cases. By **lpgamerzockt-ops** (#403).
- While a screw session runs, a bar on every page of CC2 Control shows the bed target, the time left without an action (the count stands still while a measurement runs) and the “End session” button. The status field of the header reads “Levelling” instead of Idle, and the browser asks before the tab is closed. By **lpgamerzockt-ops** (#403).
- The Bed alignment tab shows its temperatures by kind, each with its own label: the live bed value with the bed target beside it, nozzle and chamber live, and the temperature a measurement was taken at, in fixed columns; a live value is marked “not current” once the printer status is older than 10 s. By **lpgamerzockt-ops** (#403).
- Plate library: “Delete all measurements” in the ⋯ menu of a plate card (`POST /api/plates/measures/clear`, a function of its own) empties a plate on purpose; the plate stays and must be calibrated again, and the mounted plate, one waiting for the printer restart and one being calibrated are refused (409). “Delete all nozzles” does one delete per nozzle after one confirmation. Deleting a single measurement keeps its rule: a plate keeps at least one measurement (409). By **lpgamerzockt-ops** (#403).
- Bed alignment: the screw plan keeps front left as the fixed measurement reference and the default adjustment reference. The plan that also turns the front-left screw (the optimized plan, which can lower the largest turn) is no longer shown by itself: when it would help, a separate note explains that the reference screw is turned too and offers “Choose optimized correction” or “Stay with standard”. The strategy is named in the head of the suggestion block (in the notice “Four-corner measurement needed” while that block is hidden), the choice is kept in the browser tab over further measurements and a reload, and “Back to standard” is a button. What was measured, as the deviation from front left, stands in a table of its own (Z height, deviation in µm with “lies lower” or “lies higher”, and a status that says “Too low”, “Too high” or “Within tolerance” by the same value and in the same colour as the direction of the suggestion; front left carries the “Reference” badge), apart from the table with the suggestion for adjusting, whichever strategy is shown (with the optimized strategy one note under the table says that the status follows the chosen plan, which also moves front left); nothing is highlighted there before the user clicks a row. The numbered marks of the screws, the steps and the top view are one component and exact circles, also on a touch screen. With the optimized plan chosen and the front-left screw to be turned, “Measure again” becomes “Start four-corner measurement” and single corners wait; once front left has moved without a full measurement, no new plan is shown until all four corners are measured again. By **lpgamerzockt-ops** (#403).
- The print queue's job list shows the picture embedded in each G-code file, and a job is moved by dragging it by that picture, with a mouse or a finger; the arrows stay. Designed and implemented by **beanbo** (#352).
- Files and History show the picture embedded in each G-code file in every row of their lists, as the print queue does, and a job in History can be printed again while its file is known for certain to be the job's (one file of that name on the printer, written no later than the job began, in a file list the printer sent whole; otherwise the row or the list says why not): the button opens the print dialog of the Files page. On a narrow screen the file name takes a line of its own in both lists. Designed and implemented by **beanbo** (#352).
- Aurora, a light colour theme (Settings → Appearance) built so white cards stand out against a tinted blue-lavender page, with a deep indigo accent and text contrast of at least 4.5:1 on every surface. Requested and tested by **Neme77**, implemented with Claude Code (#394).
- Klipper, a flat neutral-grey dark colour theme in the style of the common Klipper web interfaces, with the Klipper red as accent, uppercase card titles and a marker on the active menu entry; text contrast of at least 4.5:1 on every surface. Requested and tested by **Neme77**, implemented with Claude Code (#395).
- Object preview on the Job page: selecting an object in the bed map, on the Objects or the Layer path tab, shows its rough 3D shape beside the map, drawn from the outer wall of at most every third layer and at most 24 layers, so the object to exclude can be recognised before it is excluded. CC2 Control reads the file once for all its objects and keeps the result in memory until 30 minutes after the last request, the start of a print of another file, a print end or a change of the file; what is kept for the file of a print that starts stays, so that file is not read a second time. A file larger than 50 MiB gets no preview, and during a print start the preview waits until the print has reported its first layer. The new `GET /api/gcode-files/object-preview` shares the layer path's single scan slot, its pause after a scan and its `/api/health` counters. Designed and implemented by **lpgamerzockt-ops** (#398).
- Spools: usage that CC2 Control could not or must not send to Spoolman is no longer thrown away or tried again for ever. It is listed on the Spools page under “Usage not sent to Spoolman” with the spool, the grams, the date and the reason: the login was changed, the address was changed, Spoolman no longer has the spool, or the amount was sent but the answer never arrived (then it is never sent twice). For each entry you choose a Spoolman spool and press Send selected, or choose Don’t send (Remove from list for the unsure ones), always after a question and never automatically. After a change of the address (another host, port or path; a slash at the end or the default port written out is not one) a line names the spools still connected to the old server, with a button that keeps those connections and one that removes them, and until one is chosen nothing is taken over from Spoolman into those spools and no usage is sent (it is collected); only the spools that were linked at the change count, not those imported from the new server afterwards; the page says once that some usage has not been sent after such a change. The Spoolman spools offered next to each entry are read in the same pages as the sync; when Spoolman could not be read at the first look, they are read again after the next successful sync, without reloading the page (at most three times in a row). A loaded list of Spoolman's spools and the spools chosen from it belong to the server settings they were read under: when the address or the login changes while the page is open they are dropped and read again, an answer from the old server that arrives late is ignored, “Send selected” is refused (nothing is sent) when the choice was made under older settings, and a “login changed” entry becomes a “server changed” entry when the address changes later (`spoolman_gen`, `gen`). New endpoints `POST /api/spoolman/held/send`, `/held/discard`, `/unlink-old` and `/keep-old`; the list is in `GET /api/spools` as `spoolman_held`, saved with the library and left out of backups. In all eleven languages. Designed and implemented by **lpgamerzockt-ops** (#425).
- `GET /api/spoolman` reports what the last Spoolman sync did (result such as unauthorized, unreachable or timeout, the grams waiting to be sent, what the spool list read and left out, spools Spoolman has archived or no longer knows). The log gets one line when the result changes, not one per cycle (#425).
- `GET /api/health` counts the layer-path work in memory: scans, requests refused as not fitting the running print, "busy" answers, bytes read, the longest and last scan in milliseconds, and the requests and scans the read hold stopped, so the load of a live layer view can be seen and compared. Designed and implemented by **lpgamerzockt-ops** (#398).
- Dune, a warm light colour theme (Settings → Appearance): sand-coloured page, cream-white cards with a firm warm edge and a deep teal accent, so white surfaces stand out without the cold blue of the other light themes; text contrast of at least 4.5:1 on every surface. Requested and tested by **Neme77**, implemented with Claude Code (#413).
- Swamp, a dark colour theme in sickly, rotten greens (Settings → Appearance): near-black moss page, olive panels and a mouldy olive-lime accent, with a cooler emerald kept for the "ok" status so it never reads as the accent; text contrast of at least 4.5:1 on every surface. Requested and tested by **Neme77**, implemented with Claude Code (#413).
- KlipperScreen, a classic dark colour theme (Settings → Appearance) in the spirit of KlipperScreen's material look: flat Material grey, large rounded buttons, menu entries and cards, no shadows and a different colour for each menu icon; the palette is an approximation, text contrast is at least 4.5:1 on every surface. Requested and tested by **Neme77**, implemented with Claude Code (#420).
- A mini player for the live camera: the Mini player button under the Dashboard's camera card opens the camera in a small window that stays above every page, so the picture stays in view while you change pages. A bar on top holds the size handle (top left corner; 240 to 640 px, at most half the window; 160 px to the window width minus 24 px on a phone, always 16:9), pause, zoom and close; the whole bar and the window frame move it, with a mouse, a finger or, at the grip, the arrow keys, nothing about it is remembered: every opening is at the standard size at the bottom right. It keeps out of the emergency stop in the header, and Escape does not close it. Pressing the Mini player button again closes it, and the Job page's camera has a Mini player button too. The picture zooms digitally up to 2x with the buttons, the keys + − 0, the mouse wheel or two fingers, and can be moved while zoomed; the camera service sends the same stream as before and sees no extra connection. It is one more viewer under the one-viewer rule, so starting the camera anywhere else (the Job page's Camera button, the camera card, the camera window) closes only the mini player, in the same browser before the new picture starts, and a hidden browser tab pauses it like the camera card; when the tab is shown again, the mini player comes back only if no other viewer has taken the camera meanwhile, otherwise it closes quietly. Designed and implemented by **lpgamerzockt-ops** (#419).

### Changed

- Layer path: a per-file layer map records where each layer starts, so a request reads that layer directly (up to 32 MiB per layer instead of the previous 128 MiB scan window). The maps are built in the background only while the printer is idle and no USB print is being prepared; uploads, copies and USB imports queue a file, deleting a file or cleaning up an internal USB copy drops its map, and a file in the printer's storage without a current map falls back to the full scan with the in-memory layer index from #398 and is queued for its map (files on a USB stick get none). A map holds at most 8192 layers (about 256 mm at 0.05 mm), a build that runs into its time limit is refused instead of storing a partial map, a build that fails (for example on full storage) is not started again by every layer request for ten minutes, and the maps live in `layer-index` next to CC2 Control's preferences (`--layer-index-dir` sets another folder), never next to the G-code; only the 64 newest maps are kept, so maps of files removed on the printer's screen or the stick cannot pile up. A minute after the start the 32 newest files of the internal storage are queued for their maps in the first idle time, and `/api/health` reports `layer_map_hits`, `layer_map_fallbacks`, `layer_map_built` and `layer_map_failed`. The stored map carries a CRC-32 of its contents (the standard CRC-32 also used for the backup files, a table-driven version in `crc32.h`) and the sizes of its records, so a damaged map, one of an older version or one written by other code is rebuilt instead of read, a request that finds no layer marker at a map's stored offset reads the file from the start instead (and the map is deleted and queued for a new one, once for each file), `/api/health` counts such requests as `layer_map_bad`, and `layer_path_bytes_read` counts a read through a map from the layer's offset instead of from the start of the file, and a file of 2 GiB or more (the limit of the printer program, see #390) gets no map and is read as before. The scan-limit refusal from #343 and #398 (413 for the same and every later layer of a file for 300 s) stays for scans from the start of the file, but no longer refuses a layer a current map serves, and a limit hit while reading through the map refuses only that one layer. Follows the per-file layer-offset index suggested in the review of #343. By **lpgamerzockt-ops** (#409).

- Spoolman: changing the server address or the login used to set the queued and in-flight grams of every spool to zero without a trace; they now go on the list of unsent usage (see above). A linked spool that Spoolman answers 404 for used to keep its grams queued and retry them every two minutes; after a 404 in two sync cycles in a row, while Spoolman's list answers, they now go on that list as well (one 404 changes nothing). In-flight grams that can no longer be settled used to be forgotten; they now stay on the list as “unsure” and are still never sent again. Turning the sync off (empty address) still discards them. By **lpgamerzockt-ops** (#425).
- Spoolman: the server has to be given as an IPv4 address (`http://192.0.2.5:7912`). A host name used to be accepted and then never reached Spoolman without any message; it is now refused when it is saved, with a text that says why (#425).
- Layer path on the bed map: the nozzle marker and its legend entry are shown only in Live mode. When you step through layers by hand the marker no longer moves over the layer you are looking at; Live draws it again. By hand every layer is also drawn whole: the path is no longer split at the nozzle and the direction arrow no longer moves with it, while Live is unchanged. By **lpgamerzockt-ops** (#410).

- The start page is no longer sent in full on every visit: CC2 Control sends an `ETag` (the SHA-256 of the page, so it follows the content and not the file's size or times) with `Cache-Control: no-cache`, the browser still asks on every visit, and a matching `If-None-Match` is answered with an empty `304`. A page that is empty or is being replaced while it is read is answered with a `503` and a JSON error. This relaxes `no-store` for `/` and `/index.html` only; API answers and `/i18n/*` keep it. Implemented by **lpgamerzockt-ops** (#406).
- Print queue jobs are bound to filaments, not to Canvas slots: each tool keeps the material and colour it was added with and prints from whichever slot holds that filament when a copy starts, so a job can be queued before its spool is loaded (in the print dialog from the spool inventory, through the API with any material and colour) and waits until a slot holds it. The job list shows each tool's filament and slot, its palette button gives a job other filaments, and every start, by the button or automatic, takes the slots from a Canvas report it asks for itself, so a spool changed just before it is followed. Designed and implemented by **beanbo** (#352).

- Installing the modified Canvas firmware (Settings → Integrations → Canvas firmware) asks with its own warning instead of the shared RFID prompt: from the confirmation until the activation is complete CC2 Control refuses printer changes, the activation needs two full reboots, a confirmation after the first one and a firmware transfer to the Canvas, and it cannot be cancelled; going back means restoring the original Canvas firmware, with a second transfer and two more full reboots. The warning has Cancel focused, so Enter no longer starts the activation. The RFID lifecycle, its actions and its guards are unchanged. Reported and implemented by **lpgamerzockt-ops** (#378).
- The Job page is one card: the job (file, progress, layer, times, estimated total time, temperatures, Pause, Resume, Cancel) above the object exclusion with its Objects and Layer path tabs, directly on the page instead of in a dialog. The camera card on this page is replaced by a Camera button that shows the picture alone beside the buttons, starts the stream when it opens, ends it when it closes and restarts it by itself when the tab is shown again; the camera window and snapshot buttons stay on the Dashboard. By **lpgamerzockt-ops** (#398).
- The console is polled with less traffic: `GET /api/console?since=<generation>:<output_len>:<instance>` (the instance names the start of the service, so a restart cannot be mistaken for an unchanged console) leaves out the output when the printer's console has not changed and no command runs (`"unchanged":true`), a request without it answers as before, and the Screw Levelling tab asks every 20 s instead of every 2.5 s while no measurement runs, closely again while one runs and for 10 s after it. Designed and implemented by **lpgamerzockt-ops** (#408).

- Bed levelling page: four tabs (Mesh & Saved Profiles, Bed alignment, Build plates & nozzles, Nozzle expansion); the load cell is a folded block of the Mesh card (`#bed/cell` opens the Mesh tab), the nozzle expansion stands in two columns, and the plate and nozzle library is rearranged: “In the printer now” with the nozzles, the library mesh and its switch, then “My plates” with equal cards, bounded lists and ⋯ menus for the less frequent actions; “Forget” on the mounted plate is now “Remove” (nothing is deleted). While CC2 Control runs a bed mesh calibration, the field at the top of every page of CC2 Control shows what it does, the time left and “Stop” (the same action as before; the message beside the buttons is gone), and the status field of the header reads “Mesh calibration” instead of Idle. The probing is counted up from 0:00 in that field, also after a page reload, and the hold stays a countdown. “Create new mesh” on the alignment page is now “Go to mesh creation”. Starting a bed mesh calibration while a screw session is open ends the session between its measurements (`end_reason` `mesh_calibration`) and is refused with 409 while one of its measurements runs. When the printer answers without a loaded mesh (its nozzle cleaning unloads it, so after every screw measurement until a print loads the mesh of its plate side again), the Mesh tab says so instead of waiting for the printer mesh. The existing parts are only rearranged and keep their confirmations; what is new on this page stands in the lines beside this one. By **lpgamerzockt-ops** (#403).
- Screw levelling: “2× 3 probes” (`screws:measure:<C>:2`, `screws:tram:start:<C>:2`) probes every corner twice for the quick measurement and the session start, the page shows one value per corner, the average of both results. The quick measurement is refused while a session runs, a second session start is refused instead of replacing the running session, and measuring one corner again homes only Z (`G28 Z`) with the bed waited for and the 30 s wait when X and Y are still homed. By **lpgamerzockt-ops** (#403).
- Screw levelling: a bed more than 2.5 K hotter than the target is cooled first, before the nozzle is cleaned, and the wait releases at the target + 2.5 K instead of up to 5 K early (the printer's `M190` accepts a cooling bed within 5 K of its target); a bed up to 2.5 K above the target starts as it is. The start asks with a note that cooling takes extra time, and the note follows the live bed value while the question is open. The session's idle limit counts only while no measurement or wait runs, and the session bar says so. “Check all four” is now “Measure again”, the per-corner button “Single measurement”. The differences beside the heights (first against second probe, change since the last measurement, moved reference) follow the chosen unit, millimetres or micrometres, and the table says nothing more about a corner than its difference. Only when the single probes of a corner in this measurement spread by more than 10 µm does it show the word “Deviation” with a small (i) that names the probes and asks to measure again. A small grey “±5 µm” (“±3 µm” with 2× 3 probes) stands behind a difference that lies that close to the 20 µm limit of the turn instruction. The probes are kept in the history (`probes` of a measurement); the firmware's own average is the value (the readings of three probes are averaged, no longer their median). By **lpgamerzockt-ops** (#403).
- Screw tramming is safer: a session now ends after 10 minutes without an action (was 20), and CC2 Control no longer clears the X/Y homing a session depends on: "Motors off" is refused during a session, and the automatic motor release after 10 idle minutes with the heaters off waits until the session has ended. A measurement in progress is not cut short by that limit. By **lpgamerzockt-ops** (#403).
- Screw-tramming history: each session records why it ended (ended by the user, emergency stop, lost connection, print start, stale telemetry, failed job, other machine state, 10-minute timeout, lost X/Y homing, quick measurement, heaters off, new session or restart; a bed mesh calibration started between two measurements ends it as well), reported as `end_reason` by `GET /api/screws/history`. Older entries report `null`. Each measurement also records its bed target, the actual bed, nozzle and chamber temperatures at its last probe and the single probes behind each corner (`bed`, `ist`, `probes`), and each session says whether it was measured at the screw centres (`points`). By **lpgamerzockt-ops** (#403).
- Bed alignment: the bed target of a screw session and of the quick measurements is chosen in step 1 as a saved mesh (its temperature), a material profile of CC2 Control (its bed temperature: factory PLA 60 °C, PETG 70 °C, ABS 100 °C; own profiles included) or an own temperature, a whole number from 30 to 110 °C, the bed limit of the printer's touchscreen. The service takes 0 to 110 °C and 60 °C without a choice (`screws:tram:start[:<C>]`, `screws:measure[:<C>]`). The session heats to the target, keeps it between probes and after a restart switches off only a bed still at it. By **lpgamerzockt-ops** (#403).

### Fixed

- The native touchscreen on the ELEGOO 02.01.01.07 base reads complete G-code thumbnails beyond 300 lines and fits large previews on the print screen, file list and print history. Reported and fixed by **beanbo** (#411).
- Spoolman: a filament colour with transparency (`RRGGBBAA`, eight hex digits) now imports as its RGB colour instead of falling back to grey; the transparency is dropped because a spool has none. Any other length is still ignored (#422).
- Spools: choosing a spool for a Canvas slot that holds filament but reports neither type nor colour (the exclamation mark on the printer's screen) now also writes the spool's material, colour and maker to that slot, as it already did for a slot reporting another filament; the question which spool it is, and the note that a spool differs from its slot, are unchanged. When nothing is written the dialog now says why in one sentence (what the slot reports already matches the spool, no material setting matches the spool's material, the printer is busy) instead of staying silent. Reported by **DooshPastesh**, implemented by **lpgamerzockt-ops** (#422).

- Spoolman: the spool list is read in pages of 20 instead of one reply capped at 128 KiB, which failed the whole import with a large Spoolman library; a spool that cannot be read is skipped and counted instead of ending the import, and a linked spool is refreshed from the same list instead of one request each. A slow server can no longer hold a request or a sync cycle open: each has a total time limit (#425).
- Spoolman: an imported spool takes the maker as its brand, the first colour of a multi-colour filament, the tare, and, where the filament has no weight, a full-spool weight worked out from what is left; a missing `remaining_weight` is worked out from the spool's start weight and use like Spoolman does. Names with JSON escapes (`\u00fc`) are read instead of turning empty. A linked spool for which Spoolman has no weight keeps its weight and still gets its name, material and colour (#425).
- Spoolman: usage for a linked spool Spoolman no longer knows (404) or refuses (401/403) is no longer queued in silence; it is reported by `GET /api/spoolman` (#425).
- Spoolman: a spool imported without a usable colour (none, or not six or eight hex digits) is shown grey; the status of `GET /api/spoolman` now counts them as `no_colour` next to `no_material`. By **lpgamerzockt-ops** (#425).
- Make every retained spool-history record accessible with translated pagination, filter reset and bounded scrolling. Reported by **DamiBFryta**, implemented by **Neme77** with AI assistance (#391).
- Keep the header title within its bar on Android desktop view and other touch devices with desktop-width viewports. Reported by **DamiBFryta**, implemented by **Neme77** with AI assistance (#392).
- Display the print-completed notice above page content instead of over queue files and controls; acknowledging it still does not start a print. Reported by **DamiBFryta**, implemented by **Neme77** with AI assistance (#393).
- Native touchscreen language follow-up: cover qualified English Home/System Version/control literals for Hungarian, Polish and Czech; adopt Hungarian reviewer wording for Unload, OFF and the model fan, retaining the compact Canvas hint. Coordinated by **Neme77**, with Hungarian review by **pezso92** and Polish user feedback; new label hooks still require physical validation before release (#383).

- Complete 698 untranslated panel entries across Czech, Polish, Hungarian, French, Spanish, Russian, Italian and Turkish, including RFID, external feed and the print queue. Preserve existing translations and technical terms; reject new English copies in locale checks and exercise all ten non-English languages in browser tests. AI-assisted translation completion coordinated by **Neme77** (#375); native-speaker wording review remains welcome.

- The spool question (“Which spool is it?”) suggests the inventory spool for a Canvas tray that names the maker’s product line: Elegoo’s Rapid PLA+ RFID spools report `RAPID PLA+`, and a spool kept as `PLA+` in a colour that reads alike is now listed and preselected instead of “No spool in the inventory has this filament”. A new spool added from such a tray gets `PLA+` as its material instead of `PLA`. A tray that names a line still leaves out other lines of its type, such as PLA Silk for PLA Matte. Reported and fixed by **beanbo** (#366).
- The compact mobile header exposes the application name as the main heading, so every page retains an accessible h1. Fixed by **beanbo** (#352).
- Dialogs accept Escape and contain keyboard focus from their first visible frame. Fixed by **beanbo** (#352).
- Queue time totals use the running copy's remaining time together with the full estimates of subsequent copies. Reported and fixed by **beanbo** (#352).
- The print queue starts the automatic countdown only when another copy is waiting with its file present. A job added after the queue finishes waits for a manual start. Reported by **lpgamerzockt-ops**, fixed by **beanbo** (#352).
- On a phone, dialogs keep the same margin on both sides instead of reaching the right edge of the screen. Reported by **lpgamerzockt-ops**, fixed by **beanbo** (#352).
- A print start from internal memory no longer holds up CC2 Control while a large file is checked, and a start on a busy printer is refused before the file is read: like a USB start, the file is checked in the background and the start is sent afterwards; the file cannot be deleted meanwhile, and a file replaced during the check is not started. Queued jobs from internal memory are started the same way. By **lpgamerzockt-ops** (#390).
- Print queue: a queued job from USB is copied in the background like a print-dialog start, so CC2 Control keeps answering during the copy; the job stays pending until the copy is done and is started then (a restart during the copy leaves it pending). The Queue page waits for the start as the print dialog does; both wait longer for a large file (at least 300 s, 500 MB: 370 s). By **lpgamerzockt-ops** (#390).
- A USB print start from the print dialog reads the file once when the dialog's own inspection of the same file is still valid: its leveling and tool check is reused, and the copy confirms it while copying; otherwise the file is checked before the copy, in one pass for leveling and tools. A start that does not fit the file is still refused before anything is copied. By **lpgamerzockt-ops** (#390).
- A print started from USB writes its copy out every 8 MiB, and drops the copy from the cache after reading it back, so the copy does not pile up data in the printer's memory; the copy no longer pauses or runs at lowered priority. By **lpgamerzockt-ops** (#390).
- A print started from USB is only sent once its internal copy is verified: the copy is refused when it would leave less than the 16 MiB storage reserve uploads keep, discarded when the USB file changed during the copy or the copy reads back different, and stopped by the emergency stop or a closed browser during the check before the copy, the copy and its read back instead of finishing first. A copy left unfinished when CC2 Control stopped is removed when it starts again and before the next USB copy; a copy still running holds a lock on it and is left alone. By **lpgamerzockt-ops** (#390).
- USB print starts no longer fill the internal storage with old copies: a start that would not fit removes the oldest copies right before its copy, once its checks passed and when that makes room, after each USB start only the newest three `CC2_USB_` copies stay, and older ones go while all copies together exceed 1 GiB. A copy that is being printed, paused or named by a queued job, or any copy while the printer state is unclear, is never removed. By **lpgamerzockt-ops** (#390).
- A USB print start is refused when a Canvas tray named in its slot mapping changed while the file was copied (spool, type, name or colour), so the print does not run with a mapping chosen before the change; start the print again. For a USB file CC2 Control asks the printer for the trays after the copy and waits up to 10 s for the answer; without an answer the start is refused as well. This holds for the print dialog and for queued USB jobs. By **lpgamerzockt-ops** (#390).
- A USB print start whose stick stops responding is answered after 15 s without progress (409, `reason: usb_unresponsive`); a slow but healthy stick is not affected. The print dialog asks to remove the stick and keeps its start button grey while the read hangs, says when it is free again and whether the read ended with an error or only late, and advises a power cycle when it still hangs a minute after the stick was removed (`usb_stall` in `/api/printer`). By **lpgamerzockt-ops** (#390).
- A USB print start with a slot mapping waits for the first Canvas report and is refused before the copy without one (409, `reason: canvas_unreported`); the print dialog stays open with a message. By **lpgamerzockt-ops** (#390).
- An analysis of a file that a USB start is still reading is refused at once (409, `reason: start_reading`), and the print dialog explains it; a start waits for the dialog's analysis of the same file. By **lpgamerzockt-ops** (#390).
- Deleting a file right after a stop is retried for up to 5 s while the printer state is still unclear. By **lpgamerzockt-ops** (#390).
- CC2 Control is built with large file support, so USB files of 2 GiB or more are listed with their size. The printer's own program opens files only below 2 GiB (2147483648 bytes, measured): the file list marks such a file as not printable, its details name the limit, and the print dialog asks before a start, with the estimated copy time and space of a USB file. Such a file is started or queued only with that confirmation (`large_ok`; otherwise 409, `reason: file_too_large_for_printer`, before anything is copied). After a confirmed start CC2 Control watches whether the printer begins it: the dialog stays open until then and says when the printer did not open the file, a queued copy is marked `not_opened`, and the internal copy of a failed USB start is removed. By **lpgamerzockt-ops** (#390).
- The active print's progress scan ends when the printer reports the print over, also without `/api/printer` requests, and files of 2 GiB or more are not scanned, so a deleted file is no longer held open until the scan reaches its end. By **lpgamerzockt-ops** (#390).
- The file list shows its tags (In queue, Not printable) together under the file name. By **lpgamerzockt-ops** (#390).

- The bed map's live layer path keeps up with fast prints. CC2 Control remembers where each scanned layer starts, and a running layer request is no longer cancelled when the printer moves on. The map keeps the last loaded layer on screen with a small loading badge instead of covering it, names the printer's layer when the shown path falls behind, dims the old path while stepping by hand, shows the print direction with a fade and an arrow, and reports a layer request that does not answer in time. Layer scans run at a lower priority with a pause after each scan, and Live no longer follows the previous print's layer right after a new print starts. By **lpgamerzockt-ops** (#398).
- A new print no longer starts with the layer, print time, total time, progress and filament use of the print before, also when it prints the same file again. CC2 Control now drops them when the printer reports a new job, and when the printer's own status stream goes from an idle state to printing; pausing, resuming and reconnecting in the middle of a print keep them. Designed and implemented by **lpgamerzockt-ops** (#398).
- The layer-path endpoint refuses a request for the running print's file before reading it when it cannot belong to the print: a layer above its layer count, or a layer above 1 before the print has reported layer 1. It answers 409, which the page does not retry, so a stale layer right after a print start no longer triggers a scan of the whole file. Requests for other files and browsing while no print runs are unchanged. Designed and implemented by **lpgamerzockt-ops** (#398).
- The layer path and the object preview read no G-code file while the printer homes, levels, probes or starts a print (before layer 1), whichever file is asked for: a long read on the printer's two small cores during the homing of a print start came with the MCU shutdown "Timer too close" (error 803). They answer 503 "Printer is homing or starting a print; retry shortly", what is already remembered is still answered, and a read that runs when the hold comes on ends at its next line. The page says "Waiting until the printer is ready…" and asks again every 3 seconds. Without a printer connection nothing is held. Designed and implemented by **lpgamerzockt-ops** (#398).
- The scan of the running print's file, which gives the layer count and the print time when the printer reports none, no longer reads while the printer homes, levels, probes or starts a print: on the printer this scan of a 52 MB file ran into the homing of the print start, and the MCU shutdown "Timer too close" (error 803) followed. It starts once the print has reached layer 1 (or the homing is over), a scan already running ends early and starts again afterwards, and it runs at a lower priority. Designed and implemented by **lpgamerzockt-ops** (#398).
- The pictures in the Files, History and Queue lists are not read from their G-code files while the printer homes, levels, probes or starts a print: `POST /api/gcode-files/thumbnail` answers 503 then, and the page lets its picture queue rest for 3 seconds before it asks for the same picture again. Designed and implemented by **lpgamerzockt-ops** (#398).
- Bed map: Live can be pressed on the Objects tab during a print; it then switches to the Layer path tab and follows the printer, and the layer arrows work only on the Layer path tab. An object can be selected by clicking its whole area, not only its number. The Layer path tab keeps the status colours and the legend of the Objects tab and adds the path and the nozzle; the numbers sit beside each object so they do not cover the path, and the path of an excluded object is pale grey. The status line keeps naming the layer while one loads, with a small mark instead of the text, and a busy answer of the service is retried after 300 ms instead of 1.5 s. Reported and fixed by **lpgamerzockt-ops** (#398).
- Manual controls refuse non-finite numbers: the console (axis targets, also in relative mode and for a repeated axis word), UI moves, extrusion, bed heater, fans and the Z offset no longer pass `nan` or `inf` on to the printer; they were accepted before. Designed and implemented by **lpgamerzockt-ops** (#417).

## [Community Firmware V4.4.4 / CC2 Control 1.2.29] - 2026-11-12

### Added

- Interactive screw-tramming on Bed → Screw Levelling: start a session with one click (full home and the existing bed prep), re-probe a single corner after turning its screw, or check all four, without repeating the full four-corner measurement. Re-probes home Z only. Every measurement, the start included, waits 30 s (`G4 P30000`) before probing so the nozzle has settled, and re-probes can probe twice to show whether it has. The nozzle is switched off after each probe; the bed stays at 60 °C until the session ends. The session survives the Manual homing / Auto leveling state its own homing and probing report, the bed target is renewed every ten minutes so the firmware's idle shutdown does not cool the bed between probes, and a bed left at 60 °C by an ended session is switched off once the printer is idle again. Requested by **sametsoyoz** (#329).
- Screw-tramming history: CC2 Control records every completed start, re-probe and check of a tramming session with the measured value of each corner and keeps the last 20 sessions on the printer, so the results are still there after a reload, the end of the session or a restart, and the next levelling can start from the last one. A one-shot measurement without a session is kept separately as a quick entry (last 10), marked as such because it has no settle. Read with `GET /api/screws/history`, cleared with `POST /api/screws/history/clear` (#329).
- Show live application update progress and a filtered installer log, and retire a failed panel status once a successful installation is confirmed. Requested by **pezso92** (#331).
- Add the Turkish interface translation using the contributed language patch.
- Read-only `GET /api/gcode-files/layer-path` returns thinned extrusion polylines for one G-code layer in bed coordinates, with the same path checks as the other G-code file routes. The Job bed-map dialog adds a **Layer path** pane (Live / previous / next, nozzle from `/api/printer`) that loads that thinned path instead of the G-code file. The current layer is requested on its own and drawn first; the previous layer comes from the browser cache or is loaded afterwards, rapid layer changes request only the layer they settle on, and a busy backend is retried instead of being shown as unavailable. Requested by **SVLoneStar** (#342).
- Print queue: prepare jobs ahead with the print dialog's new **Add to queue** choice (every print setting plus a number of copies), then start each copy with one button from the Queue page, the Dashboard or the print-ended notice, without the dialog. A pre-flight check shows the file, the filament in each mapped slot, the side's mesh, the nozzle and the printer state; copies are counted on the printer, stopped copies are not counted, and the next copy waits for the operator unless the automatic start is chosen for a self-clearing bed, which first checks that each mapped Canvas slot still holds the file's material. Designed and implemented by **beanbo** (#320).
- German interface translation (Deutsch), selectable in Settings → Language. All 1533 interface strings are translated with the informal “du”; technical terms such as Pressure Advance, Input Shaper, Load Cell, Spool, Canvas and Idle are kept. Contributed by **lpgamerzockt-ops** (#332).
- Display the current G-code layer path in the bed map with live following, bounded scan retries and scan-limit feedback. By **lpgamerzockt-ops** (#343).
- Add interactive screw tramming, individual corner re-probes, a settling pause and measurement history. Preserve pending heater shutdown across interrupted sessions. By **lpgamerzockt-ops** (#346).
- Show live application update progress and a filtered installer log, and retire a failed panel status once a successful installation is confirmed. Requested by **pezso92** (#331), implemented in #335.
- Add the German interface translation by **lpgamerzockt-ops** (#332) and the contributed Turkish interface translation (#333).

### Changed

- Complete the ten new dashboard, drying, motor-mode and Canvas validation messages in French, Hungarian, Polish, Czech and Turkish; all eleven shipped locales now cover every English key. Integrated by **Neme77** (#350).
- Screw levelling names each corner and says whether to raise or lower it against the front-left zero. The session is four steps: start, re-probe the screw you turned, check all four, then end. The one-shot measurement is marked as a measurement without a session (#329).
- Screw levelling keeps the bed at 60 °C for the session and only switches the nozzle off after start. The double measurement is labelled “2× 3 probes”. The table shows the running session’s offsets, and earlier sessions can be reviewed, exported or cleared (#329).
- Complete Hungarian translations for the RFID reader lifecycle, print queue and update requirements. By **Neme77**.
- Simplified Chinese interface strings for the RFID reader lifecycle, external feed, chamber fan management and update requirements completed, and the build-plate save hint refreshed. By **Skycos** (#328).
- Show live Z offset, the mounted plate side and the active mesh together in the existing dashboard card. Designed with **Neme77**.

### Fixed

- Re-enable exhaust flap controls, Rotation Distance and load-cell calibration after a cancelled print once the machine is Idle. Treat native `cancelled` as a terminal job state while retaining active-print, pause, heater and competing-operation guards. Reported and diagnosed with **Neme77** (#350).
- Starting a print of a large file from USB no longer stops CC2 Control from answering: the file is scanned and copied on a worker thread, so the print dialog no longer reports “print not started” for a print that then starts. Reported and fixed by **lpgamerzockt-ops** (#344, #347).
- Nord and Solarized Light keep cyan and red text readable on the bed-map dialog, including the selected object row and Exclude Selected Object. The same tokens meet the contrast on the rest of the interface. Contributed by **lpgamerzockt-ops** (#345).
- Solarized Light muted text and amber status tags meet the 4.5:1 text contrast on the page background and on the tag fill. Contributed by **lpgamerzockt-ops** (#345).
- Start preparing the qualified USB Ethernet link before the camera starts, and give the camera a bounded wait for carrier on Ethernet-enabled boots. Reported by **kalross** (#322).
- Prepare USB Ethernet activation when the minimum EXHAUST feature is active. A full idle reboot loads both components, after which the normal Connections confirmation applies. Reported by **kalross** and reproduced by **Neme77** (#323).
- Preserve the Canvas RFID lifecycle during application updates from the published CC2 Control 1.2.26 and require explicit resumption before a pending activation can flash Canvas. Allow reinstalling the same build from a PC. Reported by **Sceletor** (#313) and physically tested on a Combo by **Neme77**.
- Prepare qualified 4.4.3 installations for a firmware upgrade using coherent Canvas restoration evidence instead of a specific panel version; retain verified PC backups and refuse active, incomplete or inconsistent state. By **Neme77** (#327).
- Preserve an active, exactly qualified .13 RFID installation while replacing its three app-bound helper files, including when the app is unchanged. Keep the lifecycle journal, Canvas image, transfer worker and loaded guard intact. Requested by **Neme77**.
- Reduce RFID scan preparation work on the immutable .13 route by verifying the running native executable once before and once after the live-state check, and hashing each mapped module once per observation. Keep full process identity, profile binding and changed-journal refusal. Reported and timed by **Neme77**.
- Prepare legacy Reactor-disable markers for quarantine, archive superseded Connections compositions before firmware replacement, and retain bounded boot status files without requiring `logread`. Reported by **lpgamerzockt-ops** (#327).
- Import USB print files in a worker so the dashboard remains responsive. Cancel pending starts after a client disconnect or emergency stop. By **lpgamerzockt-ops** (#347).
- Improve Nord and Solarized Light contrast in the bed map and status badges. By **lpgamerzockt-ops** (#345).
- Preserve bed-drying timeout recovery after an interrupted stop and retry once the printer is idle with its bed target at zero. Reported by **computerkoenig** (#340).
- Keep the latest printer warning or error visible until dismissed even if a later informational report arrives. Reported by **Sceletor** (#337).
- Allow disabling Quiet motors after an application restart by restoring Normal first and saving only after the console confirms success. Reported by **kalross** (#324).
- Explain that the first Canvas RFID restart is complete and prompt the operator to continue update validation. Reported and tested on the .13 trial by **Neme77**.

## [CC2 Control 1.2.26] - 2026-11-07

### Changed

- Use one native-component installer for a supported first installation, an existing active RFID upgrade and an already current installation. Stage EXHAUST through the same authenticated application installer, retain the Canvas journal/original backup on upgrades, and restore app-bound files before rolling back the application. Developed with **Neme77** (release delivery follow-up to #299; first-install and active-update qualification completed).

- Pause panel mutations while the application installer or native maintenance coordinator owns the shared installation barrier, preserving read-only status and both emergency-stop routes; developed with **Neme77** as part of the autonomous RFID update work (#299).

- Arrange Control fan sliders above a separate Chamber fan management group, and lower Dashboard fan readouts by one element without stretching their card; designed and preview-reviewed with **Neme77** (#298).

- Reorganize Control with load-cell tools in their own Bed Levelling tab, full Rotation Distance below Pressure Advance, Input Shaper inside Movement and full-width material profiles. Add live nozzle/bed/chamber temperatures to Job details. Preserve the published Dashboard. Designed and approved by **Neme77** (follow-up to #283).

### Added

- Experimental Anycubic RFID capture, reviewed Canvas slot assignment and separate material/presence feedback, with explicit ownership, freshness and cancellation guards; developed and physically tested with **Neme77**, with decoder-layout attribution to **OpenRFID / DnG-Crafts** (#299). Includes reboot-separated activation, original-Canvas restoration, host removal and same-profile app updates; physical acceptance recorded separately from protocol ACKs.

- Continue a print from another Canvas slot after a runout, with or without the printer's auto refill: while the print waits on a Canvas runout, a card on every page offers the loaded slots (same material first) and, once confirmed, points the tools that used the empty slot at the chosen one, reheats and resumes; the printer loads it, purges and carries on, keeping the rest of the print's slot map. Optional **Match material only** under Auto refill continues automatically from a loaded slot of the same material when no slot has the same colour (it trusts the slot labels). Requested by **mcknight89**, tested on a CC2 and implemented by **Noahwhite2014** (#284).
- Build Plates: optional "Match the mesh to the print's bed temperature". Prints sent from a slicer or started on the touchscreen get the mounted plate's mesh nearest to their bed temperature, including up to 4 blended meshes between measurements (where the nearest mesh would be furthest off, written while idle and used after the next printer restart, removed again when the option is turned off), loaded one at a time after the print start's own mesh load; implemented and physically tested by **Noahwhite2014** (#296).
- Explicit opt-in EXHAUST minimum for the current print, preserving native higher cooling and releasing on Automatic, pause/end/cancel or restart; requested and physically tested by **Neme77** for #285. The complete installer stages the qualified native component and preserves it during supported app updates.

### Fixed

- Defer optional RFID staging on CC2 non Combo or currently unavailable Canvas, keeping the normal application update and EXHAUST/camera/Connections usable; the same installer can stage RFID after a later Canvas connection. Requested by **Neme77** (release delivery follow-up to #299; simulated non-Combo qualification recorded separately).

- Acquire a fresh MQTT observation before arming a read-only RFID scan when the quiet heartbeat has expired; retain the five-second acceptance limit and immediate refusal of unsafe native states. Reported by **Neme77**, verified on host/ARM fixtures (release delivery follow-up to #299; physically accepted by Neme77).
- Show the actual capture failure after a recovered readiness refusal, rather than repeating the earlier stale-cache error. Developed with **Neme77** (release delivery follow-up to #299).

- Preserve an already active Canvas during application-only RFID updates without requiring a live Canvas payload; firmware-transfer checks remain in the transfer coordinator. Retain obsolete, never-applied or fully rolled-back app preparations in a verified archive when updating their helper. Investigated with **Neme77** (release delivery follow-up to #299; physically accepted by Neme77).

- Verify and preserve installed Connections against the authenticated composed startup profile during unified updates, retaining kernel, Wi-Fi and component-file checks instead of requiring the untouched stock launcher. Reported by **Neme77** (release delivery follow-up to #299; physically accepted by Neme77).

- Acquire a fresh observation when an otherwise safe, quiet MQTT cache exceeds the five-second RFID update guard; retain the original guard, fresh native state and immediate rejection of unsafe observations. Diagnosed from **Neme77**'s captured live samples (release delivery follow-up to #299; physically accepted by Neme77).

- Keep camera and Connections payload literals outside unified-installer shell functions, reducing the instructions retained in memory while preserving the exact component archives and verification. Investigated with **Neme77** (release delivery follow-up to #299; physically accepted by Neme77).

- Read queued native telemetry before declaring a receive timeout after a delayed application turn, avoiding unnecessary UDS reconnections while retaining the five-second freshness checks. Investigated with **Neme77** (release delivery follow-up to #299; physically accepted by Neme77).

- After a native preparation observation is refused, wait briefly and retry only when the unchanged, fingerprinted Idle/cold guard passes again; retain the three-attempt bound and all original temperature, motion and freshness limits. Reported by **Neme77** during unified-update qualification (release delivery follow-up to #299).

- Keep the spool-selection question and its draft open after an accidental click outside the popup; explicit Not now, Cancel and keyboard Escape remain available. Reported and reproduced with **Neme77**, follow-up to the Canvas/spool workflow (#299).

- `/api/mesh` (the bed mesh view and the print dialog's mesh check) asks the printer only for the active mesh and reads the side and adaptive profiles from `autosave.cfg`. Its `{"bed_mesh":null}` query made the firmware serialise every stored profile (about 30 KB, up to 615 ms with more profiles) on its motion thread; opened during a first layer it ended a print with "MCU 'mcu' shutdown: Timer too close"; found and fixed by **Noahwhite2014**.
- Align manual nozzle targets, preheat profiles, material ranges and backup restoration with the Centauri Carbon 2 native touchscreen limit of 350 C; preserve valid high-temperature targets after manual homing. Native thermal protections and separately qualified PID limits are unchanged. Reported by **Sangra6969** and reproduced by **Neme77** (#290).

## [Community Firmware V4.4.3 / CC2 Control 1.2.25] - 2026-11-03

### Fixed

- Keep computer-assisted update payloads on persistent UDISK, verify an exclusive stage and archive, retain logs/backups and clean payloads only after success. Refuse insufficient RAM before transfer and before parsing embedded components; fixes the Windows-installation OOM observed and recovered by **Neme77** (#287).

- Stop presenting a completed older application update as the current selection after a full firmware upgrade; retain failed/current jobs, logs and rollback backups. Reported by **Neme77** (#287).

- Keep the Obsidian background continuous on the short Appearance and About pages, without repeating horizontal bands or changing their layout; reported and verified in the browser by **Neme77** (#287).

- Recover Connections updates after an exact supported base-firmware reflash while retaining saved settings and exact rollback; preserve the previous component when its backup move fails. Correct RTL8153 USB interface-parent checks behind hubs without changing driver/probe/library binaries; reported and physically investigated by **Neme77** (#286).

- Refuse to save a build-plate measurement unless the current native mesh calibration positively completes; failed Z homing, firmware errors and acknowledgement-only replies preserve previous measurements (#279, reported by @Noahwhite2014).

### Added

- Release complete Community Firmware 4.4.3 with Control 1.2.25, the native display and firmware-shipped qualified RTL8153 modules; retain the stock kernel, native startup/resource synchronization and saved connection/calibration settings. The integrated firmware and corrected persistent-staging PC installer were accepted by **Neme77** before publication (#287).

- Live runtime Pressure Advance controls alongside speed/flow, plus configurable Open/Close flaps and navigation shortcuts for Connections, Fans, Pressure Advance and bed/chamber heating. Keep the four slots and existing choices; no load-cell shortcuts. Clarify bed/chamber heating copy in all nine languages; implemented and tested by **Neme77** (#286).

- Optional native display refresh with Obsidian/cyan styling, preserved native functions and illustrations, and read-only inline LAN IPv4/link information; owner-supplied firmware inputs remain private (**Neme77**, #282).
- Native exhaust-grille Open/Close controls beside EXHAUST and an advanced calibration card for rotation distance and load-cell read/diagnostic/tare/guided calibration, with guarded native sessions, verified saves and protected restart; implemented and physically tested by **Neme77** (#283).

- Experimental nozzle thermal-expansion calibration on the Bed Levelling page (disabled by default, enabled from Experimental integrations): runs the firmware's NOZZLE_TEMP_CALIBRATE over a chosen range (default 130–330 °C, 10 °C steps, keeping the 140 °C reference and printing temperatures inside the table), always switches the nozzle off afterwards, keeps the table only when it is plausible and applies it after a printer restart, with a way back to the factory table. The factory table ends at 250 °C, where the firmware switches to a constant ~0.10 mm correction, so materials printed above it needed their own Z offset; by **Noahwhite2014** (#273).
- Explicit experimental opt-in for Realtek RTL8153 USB `0bda:8153` revision `3000`, with a revision-specific preliminary ABI probe and unchanged connection confirmation/recovery. The tested `31fd` path remains available; other revisions are not automatically enabled. Reported by **kalross** (#274), implemented by **Neme77** (#278).

- Experimental chamber preheat as a mode of bed heating: the bed heats the empty chamber with the exhaust grille closed and the aux fan tapering off near the target, reports when the chamber is ready, estimates where it will level off, hands the bed to a print that starts and restores the grille, fan and bed idle timeout afterwards; requested by **Sceletor** (#270), by **Noahwhite2014** (#277).

## [CC2 Control 1.2.24] - 2026-10-31

### Added

- Optional manual Wi-Fi/USB Ethernet Connections page with saved Wi-Fi profiles, DHCP/static IP, interface switches, persistent selection and Diagnostics; qualified TP-Link UE300 RTL8153 profile (USB 0bda:8153, revision 31fd), implemented and physically tested by **Neme77** (#266). Wi-Fi remains available without an adapter. The original display-IP experiment is excluded.

- Delete unused G-code files while printing or busy, with active/paused/recoverable job and pending Orca-start protection, requested by **Neme77** (#259).

- Optional Obsidian theme with graphite panels, a purple action accent and cyan telemetry, designed with **Neme77** (#255).

- Hungarian companion to the external-feed experiment guide, by **pezso92** (#249).

### Fixed

- Detect overwritten pending Canvas pull-back and plate-mesh edits after native `SAVE_CONFIG`, refuse incompatible calibration/saving and guard the delayed restart; preserve newer native settings and require an explicit re-save. Reported by **Noahwhite2014** (#267), corrected by **Neme77** (#266).

- Stage the cumulative Connections installer on persistent storage, bound local status reads and retain logs/backups while cleaning payloads only after complete success; preserve native DHCP, camera/reactor startup and same-boot qualification caching verified by **Neme77** (#266).

- Emergency stop now goes straight to the firmware's emergency_stop endpoint, so it acts at once during homing, bed meshing, heat-and-wait or any other long command and while another console command is busy, instead of waiting behind it or being refused; by **Noahwhite2014** (#264). Maintainer review adds a two-second socket deadline and rejects incomplete or oversized replies so a saturated socket cannot freeze the panel or silently suppress the M112 fallback.

- Translate file-deletion refusal reasons in all nine languages, including bulk deletion and unknown-error fallback; reported by **pezso92** (#261, #262).

- Centre collapsed navigation and progressively clip labels, by **Surfoo** (#257); preserve labels in mobile drawers and keep the expand icon visible.
- Thicken the Canvas colour rings while preserving their outer dimensions, by **Surfoo** (#258).

- A bed mesh calibration whose heat-up and soak together took more than about 15 minutes stopped with "The bed did not hold the temperature" before probing: the firmware switches an idle printer's bed off 15 minutes after the last command. CC2 Control now sends the bed target again every ten minutes while it heats and soaks, by **beanbo** (#256).

- Keep desktop page and camera widths unchanged when collapsing the sidebar, centring content in the extra space instead of stretching it, requested by **Neme77** (#255).

- Clarify firmware-managed Input Shaper saving and the explicit Save PID step in all nine languages, following Neme77's calibration and reboot verification (#252).

- Restore Hungarian accents in the signed 1.2.23 update summaries on both catalogue routes; wording and report by **pezso92** (#248, #250). Application version and downloads are unchanged.
- Z move buttons were inverted: the up arrow now raises the bed and the down arrow lowers it (#253, by **Surfoo**); retain arrow-only controls and protect both click directions with a browser regression.


## [CC2 Control 1.2.23] - 2026-10-27

### Added

- Experimental native Canvas / external-feed selector with persistent external material controls, guarded empty-head switching and mode reassertion before external loading; direct PLA path validation by **Neme77**, full PLA to TPU cycle including eSUN TPU 95A printing validated by **DamiBFryta** without unplugging Canvas (#243).

### Fixed

- Allow native Canvas data setters before full XYZ homing without relaxing motion guards (#241, reported by **Neme77**).
- Match native `local/` completion paths to MQTT names before showing and replaying a completed job (#242, reported by **Neme77**).

- Point the stable update notice to Settings → Integrations in all nine languages; experimental controls are no longer described as being below the update panel (#240, reported by **Neme77**).

### Changed

- Polish Hungarian installer prerequisites and Canvas labels for more natural wording (#238, by **pezso92**).

## [CC2 Control 1.2.22] - 2026-10-26

### Added

- Spools: optional Spoolman sync pushes measured usage to a self-hosted Spoolman server and pulls material, colour, name and remaining weight back; usage whose delivery is uncertain is never sent twice, and exported backups leave out the proxy login (#208, by **tharje**).

- Optional standalone Windows/Linux/macOS installer for the fingerprint-pinned extended Canvas pull-back range, with original-file backups and guarded removal; original patch and V4.4.1 R3 install/removal validation by **Noahwhite2014** (#172, #202, #228). Exact V4.4.1/V4.4.2 runtime profiles; CC2 Control 1.2.21 or 1.2.22. The separate component is never installed by the application updater.

- Native external-spool Load/Unload controls and shared Canvas/external Brand, Type, Subcategory and Colour editing, with the stock material catalog and 24-colour palette; requested and investigated with **Neme77** (#233).

- Independent Spoolman integration switch in Settings, hiding its spool controls and pausing server requests without losing local tracking, saved server settings, links or queued usage (#227; requested by **Neme77**, building on [tharje](https://github.com/tharje)'s #208 integration).

- Optional browser-local LAN code remembering in Settings → Connection for updates and experimental controls, with verification, masked fields, explicit forgetting and unchanged action confirmations; requested by **Neme77** (#225).

- Opt-in experimental timed bed heating for filament drying, with finite duration, explicit stop/readback and a short native heater lease; requested by **romeopuch** in #198 (#218). No automatic heating after restart. Maintainer tests at 50 °C / 2 minutes confirmed heating, manual stop and normal expiry with zero-target readback; the feature remains experimental.

- Nonblocking completed-print notice with acknowledgement and Print again through the normal preparation dialog, inspired by the printer display and requested by **Neme77** (#219). Uses existing polling; cancellation/disconnection is not success.

### Fixed

- Bed-drying documentation now describes the native help response and separate command/readback verification correctly; clarification by **pezso92** (#235), following **Noahwhite2014**’s diagnosis in #230.

- Extended pull-back installer: identify the configuration file/value preventing installation and retain removal backups under a clean token-based name; reported and R3 printer-tested by **Noahwhite2014** (#228).

- Align experimental-integration card actions and place the drying controls side by side on desktop and phone; reported by **Neme77** (#232).

- Bed drying: distinguish a manually stopped session with fresh zero-target confirmation from an external or unconfirmed interruption; reported and printer-tested by **Neme77** (#230).

- Experimental bed drying: accept the real firmware help response, which omits M-codes, while preserving the native heater lease, command acknowledgement and target readback checks; reported, tested and diagnosed by **Noahwhite2014** (#230).

- Reset the current-job progress display when the printer is idle, without homing or overwriting the native last-job counters (#219).


- Updates: include supported Community Firmware V4.4.2 in the requirements notice across all nine languages; strengthen the update-panel regression and cover Spanish language switching (#222, fixes #221). Reported by [pezso92](https://github.com/pezso92).

### Changed

- Control: place live Z offset above homepage-style print tuning, compact machine actions beside calibration cards, and remove the redundant fan entry hint (#226; layout by **Neme77**, inspired by [pezso92](https://github.com/pezso92)'s #187 design work).

- Dashboard: integrate pezso92’s paused-camera artwork and speed/flow sliders with the existing numeric Apply/Reset controls; move “Keep active” into the camera header, compact Quick actions and align the desktop columns (#223, visual direction and artwork by [pezso92](https://github.com/pezso92) in #187).
- Settings: group the independently enabled experiments below official Integrations instead of Updates; include timed bed heating from #218, keeping its existing guards and opt-in behavior (#223).

- Spools: keep recent movements in a bounded scrolling panel, with All movements / Consumption only filters and visible result counts; saved accounting and per-spool history are unchanged (#220). Requested by [DamiBFryta](https://www.reddit.com/user/DamiBFryta/).

## [CC2 Control 1.2.21 / Firmware V4.4.2] - 2026-10-24

### Added

- Firmware builder V4.4.2: integrate the qualified R2 camera thread-reaping and local/Matrix recovery runtime from first boot, with exact-fingerprint checks, migration of the actual published R2 component and native fallback with a current-boot diagnostic (#214). Thanks to [tharje](https://github.com/tharje) for diagnosis and [DamiBFryta](https://www.reddit.com/user/DamiBFryta/), our ultimate beta tester, for persistent testing. Includes CC2 Control 1.2.21; the complete OTA is built locally. The final R4 image passed maintainer first-boot, local/Matrix video and completed-print testing with AI enabled.

- Experimental Canvas pull-back: verify the exact running extended-range firmware before allowing values above 350 mm (#209, original contribution by Noahwhite2014 in #202 and issue #172); runtime installation remains separate and is never activated by an application update.

- Spool insertion popup: add an identical spool without moving the original, and calculate remaining filament from total and empty-spool weights while keeping nominal capacity separate (#210). Suggested by [DamiBFryta](https://www.reddit.com/user/DamiBFryta/), our ultimate beta tester; the Spools section stays unchanged.

- Canvas: an explicit action to recover an unassigned filament head by loading any present slot (#190).
- Files: older G-code files stay reachable, and uploads stream up to 512 MiB (#197).
- Object exclusion: a single refined 2D bed map replaces the previous views (#199), following DamiBFryta's feedback and Nick Plateringen-Paul's original map idea/prototype.

### Fixed

- Application updater: accept GitHub redirect security headers larger than 8 KiB within the existing 16 KiB total budget, while preserving TLS and redirect checks; recognize a qualified dormant R2 camera directory after rollback or firmware reinstall and retain it before reinstallation; prefer a verified canonical camera installation over retained personal-trial files (#214). Reported and reproduced during maintainer upgrade testing; independently diagnosed by efiten in #215.

### Changed


- Experimental feature settings: remove unrelated extended pull-back/camera and memory-saving wording, and show a contextual Control shortcut when Normal must be confirmed after a Canvas source change (#214, maintainer feedback).

- One application update stream replaces the experimental channel selector, including for browsers with the old channel preference saved (#214). Optional experiments are independent from application updates: independent, persisted Canvas UDS (#200) and Quiet-control opt-ins (original motor feature by BorjaLL, #179), both initially disabled. Normal must complete before hiding Quiet controls or reconnecting Canvas while those controls are enabled. Extended pull-back remains a separate firmware experiment.
- Public cumulative release packaging with `--release` refuses missing qualified camera runtime files, retaining the camera migration for users skipping 1.2.19.
- Canvas: compact slot cards, shared actions and a bounded session-history panel; inspired by Surfoo's layout proposal (#203) and refined through the maintainer-approved layout (#207).

- Web UI: shared design system (page, card and dialog titles, spacing, radius), documented in `AGENTS.md`, with keyboard focus, accessible names and landmarks checked with axe (#196).
- The demo (`npm run preview:ui`) starts in English.
- Homepage: simplified, with the firmware and CC2 Control downloads highlighted (#201).

## [CC2 Control 1.2.19] - 2026-10-21

For Community Firmware V4.4/V4.4.1 (ELEGOO base 02.01.01.07). Settings and libraries are retained. **A full printer reboot is required after installation**: the camera fix activates at the next complete printer start. The firmware image and builder downloads are unchanged and will be updated separately.

### Added

- Interactive object map in Job → Object exclusion → Bed map: 256 × 256 mm view, numbered markers and outlines, linked list. Exclusion keeps its confirmation and original G-code ID; no extra polling. Idea and prototype by Nick Plateringen-Paul (#188).
- Panda/DragonBreath: a read-only port 7125 bridge reports the filename and available filament/usage metadata for DragonBreath AUTO zones. It is not direct MQTT heater control. By Noahwhite2014 (#181, #182).

### Changed

- Files with invalid or epoch dates show the date as unavailable, and undated files sort after valid dates. G-code and original timestamps are untouched (#185).
- Canvas and mobile refinements by pezso92: no save on an unchanged Enter, no duplicate restart submission, slot observations annotated with recent printer context, header actions and the heater link kept accessible on phones (#175, #176, #177, #180).

### Fixed

- Camera: reclaim the stacks of finished video-client threads and restore the readiness notifications that local/Matrix video needs after a camera-only restart through the patched launcher. The original camera executable is retained; there is no watchdog, automatic restart, extra stream slot or Wi-Fi tuning. Diagnosis and reproduction by tharje (#160, #183, #186).

Thanks to **tharje, Nick Plateringen-Paul, pezso92, Noahwhite2014**, **Barry**, and **[DamiBFryta](https://www.reddit.com/user/DamiBFryta/), our ultimate beta tester**, for their contributions and testing. Earlier Spanish (**asiersan**), Hungarian (**pezso92**), punctuation (**Surfoo**), plate/mesh (**beanbo**) and other contribution credits remain in [Community credits](docs/CONTRIBUTORS.md).

## [CC2 Control 1.2.18] - 2026-10-19

Only Community Firmware V4.4/V4.4.1. Update directly from an earlier compatible version; preferences and libraries are retained.

### Added

- Canvas pull-back after the cut: set the native 60–350 mm distance, with backup, restore-default and restart now or later. Needs an idle printer and applies after restart. By Noahwhite2014 (#168).
- Panda Breath / Dragon Breath page in Settings → Integrations, embedding the heater's own web page. It does not use MQTT. By Noahwhite2014 (#166, #167).
- Canvas recovery guidance: explains the native display recovery when a reboot leaves filament in the head without an assigned slot. No automatic slot selection, heating or feeding (#162).

### Changed

- 88 Spanish, Russian and Simplified Chinese strings completed, and updater compatibility wording clarified in nine languages. By pezso92 (#163, #164, #165).
- Stricter configuration parsing, restart-tracking and write-failure guards, and consistent Panda Breath / Dragon Breath names (#169, #170).

Thanks to **Noahwhite2014**, **pezso92**, **Barry** for Panda Breath field feedback and **[DamiBFryta](https://www.reddit.com/user/DamiBFryta/), our ultimate beta tester**. All previous author/language credits remain in [Community credits](docs/CONTRIBUTORS.md), including Spanish by **asiersan**, Hungarian by **pezso92**, punctuation by **Surfoo**, and mesh/plate work by **beanbo**.

## [CC2 Control 1.2.17] - 2026-10-18

Only Community Firmware V4.4/V4.4.1. Application update; firmware and callback unchanged. Features by pezso92 (#155, #156, #157, #158).

### Added

- Manual diagnostics export from Settings → About, with an explicit allowlist (version, connection and memory status, temperatures, Canvas counts). No credentials, addresses, filenames or raw API responses; no automatic upload.
- Observed Canvas timeline: up to 24 recent slot-state changes kept in browser memory, with a clear button and no additional periodic requests. It resets on reload and is not a physical-movement log.

### Changed

- Orca connection checks test the real Moonraker and Canvas lane endpoints and distinguish a disconnected printer, incomplete lane data and endpoint errors. This helps diagnosis; it does not guarantee that every OrcaSlicer version synchronises.

### Fixed

- Diagnostic Canvas envelope, ambiguous timeline rows rejected, repeated Orca tests guarded, and the timeline icon registered to prevent a rendering failure.

Retains backup/restore, languages and all earlier fixes, including Spanish by **asiersan**, Hungarian by **pezso92**, punctuation by **Surfoo** and plate/mesh work by **beanbo**. Thanks to **[DamiBFryta](https://www.reddit.com/user/DamiBFryta/), our ultimate beta tester**, and all contributors/testers. Experimental Canvas ejection remains excluded.

## [CC2 Control 1.2.16] - 2026-10-18

Only Community Firmware V4.4/V4.4.1. Firmware, callback and configuration are retained.

### Added

- Backup and selective restore of preferences, material presets, saved plate/nozzle meshes and spool inventory/history, with a preview and category selection. Credentials, firmware, G-code/video and official A/B meshes outside the library are excluded; spool tracking starts disabled after a restore (#144, #145, #148, #147).
- Optional Console monitor: bounded passive G-code reports over the existing connection. It is not the full system log (#149).
- Print mesh status (saved, confirmed missing, unknown) with a read-only Refresh, by pezso92 (#150, #151).

### Changed

- The saved language loads before the meaningful interface render, with bounded requests and a fallback. No offline cache was added.
- Hungarian: 42 refinements and failed-save feedback by pezso92. The language menu is ordered alphabetically.
- All six Settings tabs stay readable and wrap on narrow screens.

### Fixed

- A failed mesh read no longer silently selects calibration, and A/B status is independent.

**Thanks to [pezso92](https://github.com/pezso92)** for Hungarian, preference-save feedback and print mesh readiness. Special thanks to **[DamiBFryta](https://www.reddit.com/user/DamiBFryta/), our ultimate beta tester**, for the backup/language ideas, first error 803 report and tireless tests. Retains Spanish by **asiersan**, punctuation by **Surfoo**, mesh work by **beanbo** and all previous credits. Experimental Canvas ejection remains excluded.

## [CC2 Control 1.2.15] - 2026-10-17

Only Community Firmware V4.4/V4.4.1. Application update; installed firmware, callback patch, preferences and profiles are retained. V4.3 and stock firmware are unsupported. Integrated in #142 (#138, #139, #140, #141).

### Added

- Update notification bell beside Emergency Stop for a newer compatible update from the signed catalog. It opens Settings → Updates and never downloads or installs automatically.
- Library mesh selector: choose a saved measurement by plate, side, temperature and timestamp. **Mount and activate** confirms the physical plate and verifies the active profile, matrix and bounds before reporting success.
- Selectable calibration hold from 0 to 60 minutes after the target temperature (2 minutes by default).

### Changed

- 20 remaining Canvas, object-position and plate/calibration Hungarian translations by pezso92.
- The selected calibration destination and timing are kept when changing pages. Restart is requested only when needed, with separate consent.

**Thanks:** **[pezso92](https://github.com/pezso92)** for Hungarian and **[beanbo](https://github.com/beanbo)** for the underlying plate/mesh library. A special salute to **[DamiBFryta](https://www.reddit.com/user/DamiBFryta/), our ultimate beta tester**: the first to report error 803, and still tireless in reproducing problems, checking fixes and lending us his time and printer. Thank you to every contributor and beta tester; [full community credits](docs/CONTRIBUTORS.md).

## [CC2 Control 1.2.14] - 2026-10-16

Only Community Firmware V4.4/V4.4.1; do not install on V4.3 or stock ELEGOO firmware. Preferences and profiles are retained.

### Added

- Plates can be created before their first mesh and selected for calibration. Calibration works from 30 to 110 °C, and Side A/B labels are shown for default/default1.
- Measurement date with exact time and timezone in mesh details.
- Spanish by asiersan (#130, #133), and Hungarian refinements by pezso92 (#129, #132). Nine languages are now supported.

### Changed

- Object Exclusion lists objects from front to back and left to right with readable names and numbered copies, keeping original G-code identifiers separate. It handles ELEGOO's string-encoded coordinates and clears completed-print objects.
- Canvas state distinguishes empty slots, filament present but unloaded from the head, loaded filament and transitions. Actions are enabled only when applicable.
- Localised punctuation by Surfoo (#128, #135).
- Advanced mesh builds on beanbo's #104.

### Fixed

- Persistent live camera keeps its original size. Fan sliders drag correctly and numeric values still apply with Enter.
- Uncalibrated profiles cannot be mounted.

### Notes

- Library compatibility: older applications enforcing a 40 °C minimum cannot read measurements created at 30–39 °C and may discard uncalibrated profiles when saving. Back up the plate library before reverting.
- Experimental Canvas ejection, continuous Canvas UDS and webcam-service restart are excluded.

**Credits:** Spanish by **[asiersan](https://github.com/asiersan)** ([#130](https://github.com/Neme77/centauri-carbon-2-community/pull/130), integration #133); Hungarian refinements by **[pezso92](https://github.com/pezso92)** ([#129](https://github.com/Neme77/centauri-carbon-2-community/pull/129), integration #132); localized punctuation by **[Surfoo](https://github.com/Surfoo)** ([#128](https://github.com/Neme77/centauri-carbon-2-community/pull/128), integration #135). Advanced mesh builds on **beanbo's #104**. Thanks to the beta testers for the reports and real-printer trials; [full credits](docs/CONTRIBUTORS.md).

## [CC2 Control 1.2.11] - 2026-10-14

Stable application update (#127). Signed package SHA-256: `2d08659473b4245bf6f43434857a143338af4342dfd6c40da19b8d26faa23074`.

### Added

- A signed stable catalog for discovery, download/verification and separately confirmed installation from the panel, with the configured LAN code.

### Changed

- Localised descriptions come from the signed catalog in all eight app languages without another catalog fetch, with an explicit English fallback label.
- Settings → Updates hides the descriptions of installed and older releases.

## [Community Firmware V4.4.1] - 2026-10-14

Includes CC2 Control 1.2.10 (#126). Firmware artifact SHA-256: `61356c3a83be374988d6154d429c27c403985974663d3780b194164e774a22c1`.

### Added

- Settings → Updates works from the first installation, without a separate computer-assisted bootstrap: the verifier, HTTPS downloader, CA bundle, public key, runner and notices are staged in the immutable rootfs.
- The advanced plate/nozzle library is explicitly persisted under `/opt/usr/cc2-control/bed-plates.json`.
- First-installation and update guides in English and Italian, explaining the separate LAN-code authorisation.

### Changed

- ELEGOO base 02.01.01.07, the native callback and firmware patches, and compatibility family 4.4 are preserved.

## [CC2 Control 1.2.10] - 2026-10-14

Only V4.4. Merged scope: #104, #115, #116, #117, #118, #119, #120, #121, #124.

### Added

- Hungarian by pezso92; advanced temperature/nozzle mesh with soaked calibration by beanbo; V4.4-only installer with preservation and backup checks.
- Available internal and mounted USB space is shown; Files and History are paginated at 15 items without extra page-driven requests.
- Persistent camera in Settings → Appearance: one original stream across navigation and hidden pages, with manual Start/Pause, disable, close and viewer handover.
- Signed GitHub update discovery, bounded HTTPS download and verification, separate installation confirmation, signed local upload, real firmware fingerprints and configuration-preserving installation.

### Changed

- Old Canvas mappings are cleared before external-feed starts, and calibrated multicolour mappings are sent together (beanbo). Canvas telemetry stays on MQTT.
- Orca bed-temperature estimates are corrected and all filament tool masses are summed. Unknown estimates stay unavailable.

### Fixed

- The empty file-analysis notification and close icon no longer appear when the Canvas print dialog opens.

## CC2 Control 1.2.5 - 2026-10-13

Application update for Community Firmware V4.4.

### Added

- Complete Hungarian interface by pezso92 (#115): 879 entries and placeholders, included in the pinned builder snapshot, with tests for selection, saved preference, hu-HU detection and mobile layouts.
- Bed mesh calibration with a bed temperature (40–110 °C, 60 by default), a soak time (10 minutes by default) and a nozzle, saved to a build plate. It runs on CC2 Control, so the page can be closed, and it can be stopped until probing starts. A plate keeps one measurement per temperature and nozzle, and a print uses the one nearest to the file's bed temperature (or the one chosen in the print dialog). A nozzle list adds each nozzle's Z correction to the plate's Z offset. By beanbo.
- The installer checks the installed and running fingerprints before stopping the service and refuses V4.3, original firmware and unknown bases. It needs a connected, fresh Idle state and cold heaters with zero targets.

### Fixed

- The printer's Canvas colour table is cleared before a print started on the external feed, as the touchscreen does. Before, a directly fed print (for example TPU with the Canvas cable unplugged) could pause at layer 0 with error 1210 or 1220. The print does not start unless the printer acknowledges the clear.
- The whole Canvas map of a multicolour print started with bed calibration is sent in one colour-table command. Before, only the last tool stayed mapped.

## [Community Firmware V4.4 / CC2 Control 1.2.3] - 2026-10-13

Built on official 02.01.01.07 only. Complete signed firmware, public source builder and SHA-256 checksums are in the [V4.4 release](https://github.com/Neme77/centauri-carbon-2-community/releases/tag/V4.4). CC2 Control 1.2.3 is included in the firmware; do not install this application separately on V4.3.

### Added

- Quick Actions for PID and Input Shaper, restored Input Shaper tools, and guarded hotend/bed PID calibration.
- Fan percentage inputs (0–100) applied with Enter beside the sliders, with validation and readback.
- Confirmed reset of material, colour and temperature records for all four Canvas slots while idle. It does not unload filament or delete the spool inventory.
- Complete Polish and Czech translations. The interface supports English, Italian, French, Simplified Chinese, Russian, Polish and Czech, with six themes.
- English and Italian first-installation and OrcaSlicer/Canvas guides, linked from the homepage.

### Changed

- Firmware patches ported to the new base, keeping the qualified O0 callback runtime, authentication, native camera integration and protected live Z offset.
- Material profile names accept internal spaces, including in Canvas commands, with the 16-character limit and command-character guards kept. The preheat button row is replaced by a full-width themed selector.
- Live camera streaming continues while the page scrolls; manual stop, hidden-tab suspension and viewer handover are retained.
- Better G-code filament colour parsing and Canvas print mapping explanations. Empty slot icons are dimmed and neutral according to printer status.
- Control panel cards are aligned.

### Fixed

- LAN/WAN transitions and registration recovery. Native light commands receive correlated acknowledgements and synchronise the printer display, and own-printer PONG freshness keeps the connection indicator aligned with the link.

### Notes

- Beta validation reported 40-hour active printing sessions without reported problems and more than 20 confirmed long tests. These reports cover the beta cycle, not an exact published artifact.
- Excluded: PR103/104 and the experimental full Canvas UDS 1.2.4 variant. The existing build-plate library is included; the temperature/nozzle mesh extension of PR104 is not.

## [Community Firmware V4.3 / CC2 Control 1.2.0] - 2026-10-09

Pre-release: signed firmware, standalone callback updater, updated source builder and checksums. The source includes the effective-Z-offset wording clarification, which the attached tested binaries predate.

### Added

- Fingerprint-gated experimental O0 callback runtime for vendor memory retention, with native preflight, fallback and a disable switch. The request log remains unbounded, and encouraging hardware reports do not prove every OOM/803 cause is resolved.
- Standalone CC2 + callback updater without reflashing, preserving the spool inventory during rollback.
- Hotend/bed PID calibration, printing temperature changes, and printer error popups that do not recur solely from historical events. PID and error reports are translated into Russian and Chinese.
- Spools page tracking filament spools in the Canvas slots and on the external holder, charged with what the printer really extrudes (purges and cancelled prints included). New filament in a slot asks which spool it is, a spool that runs out is set to zero, weigh-ins correct the count, and the print dialog warns when a spool holds less than the file needs. Off until switched on.
- Build-plate library in Bed Levelling: save each plate's mesh and Z offset once and mount the plate later. A plate whose mesh is not in its side's slot is written there and the printer restarts.
- Large G-code files are analysed asynchronously with a bounded queue and cache.

### Changed

- Bed mesh calibration homes X, Y and Z first when the printer is not homed, instead of being refused by the console.
- An OrcaSlicer upload whose name already exists is saved as `name (1).gcode` instead of failing with HTTP 409; nothing is overwritten.
- Fewer requests that feed vendor memory retention: `info` is requested only after three seconds of telemetry silence, instead of every two seconds. The Job page takes the excluded and current objects from the telemetry stream, asks for the object list once per job, and an idle Job page asks for nothing.
- CC2 Control starts 30 seconds after the vendor process appears.

### Fixed

- CC2 Control registers with the printer even when another app's Canvas reply arrives first after it connects. Before, print and Canvas commands were refused and the updater rolled back.
- The mainboard fan no longer runs on after homing or calibration: `M84` is sent after ten idle minutes with the heaters off, as stock Klipper does.
- Expiry of queued complete HTTP requests.

## CC2 Control 1.1.31fix - 2026-10-04

Standalone update. The internal service version remains 1.1.31, and it does not replace the V4.2-R5 firmware release.

### Added

- Windows launcher with explicit SSH host-key recovery, forwarding command-line arguments.
- Print setup shows each G-code tool's filament colour and material beside its Canvas slot selector, with a fallback when metadata is missing.
- Timelapse recording selectable in the print popup, including calibrated starts.
- Deletion of individual or loaded completed print-history entries, with confirmation and Idle guards.
- Live camera viewing coordinated across CC2 Control windows; failed ownership requests do not open a stream.

### Changed

- Files lists the 128 newest G-code files of a storage, says when older files are hidden, and shows the upload card above the list.
- Automatic Canvas discovery is bounded across MQTT reconnects, with diagnostic counters; explicit Sync remains available.
- Object-query UDS sessions stay open and incomplete replies are rejected, reducing connection churn when opening Job.
- Dashboard Quick Actions show the real internal-light state. All-heaters-off commands need fresh idle telemetry and are disabled while printing.
- The updater waits up to 240 seconds for startup and registration, and CC2 Control starts 60 seconds after `elegoo_printer` is detected.
- Native timelapse rendering is followed until machine state 12 ends before the history refreshes.

### Fixed

- Intermittent uploads when headers and file data arrive together; duplicate-file refusals and printer-refused browser uploads are reported clearly instead of as a network error.
- 3D mesh viewing, including meshes with large height offsets and camera rotation.

## CC2 Control 1.1.31 - 2026-09-27

### Added

- OrcaSlicer Moonraker-agent Canvas filament synchronisation through the read-only `/server/info` and `/server/database/item?namespace=lane_data` endpoints. Thanks to @efiten for the original proposal, investigation and printer-side validation.
- LAN access-code replacement and revalidation from Settings → Connection.

### Fixed

- LAN-code changes are written atomically and restart only CC2 Control, leaving printer services and an active print untouched.
- Canvas tray material, colour and nozzle temperature are exposed to OrcaSlicer without a new polling loop or MQTT subscription.

## [V4.2 / CC2 Control 1.1.30] - 2026-09-27

### Added

- Unified Bed Levelling workflow, Side A and Side B saved-mesh visibility, and screw corrections in microns with a guarded reference optimisation.
- Operational dashboard Quick Actions and a global emergency stop.
- Compact settings panels and persistent dark/light themes.
- Four more colour themes (Dracula, Nord, Monokai, Solarized Light), chosen in Settings → Appearance and saved on the printer; `/api/preferences` accepts any lowercase-hyphenated theme identifier.
- Redesigned web interface: full-width header with the emergency stop, a side menu that collapses to icons, a link for every section and Lucide icons throughout; one printer-link indicator instead of two always-green badges.
- In-page confirmations, red dismissible errors, print progress in the browser tab title, drag-and-drop G-code upload and command history in the console.
- The machine state is followed by what the printer is doing within it (for example "Printing · Heating bed"), using the vendor sub-state codes. Printer refusals of MQTT requests are shown with the vendor meaning of their code, and `/api/printer` reports the latest one as `printer_error`.
- Canvas → Auto refill shows and changes the setting that continues from another slot with the same filament when a spool runs out. Its readback is invalidated on MQTT disconnect and requested again after registration.
- New History page: print jobs recorded by the printer with start time, duration and result; ready time-lapse videos can be downloaded and recorded frames rendered while the printer is Idle.
- Translations keyed by identifier (`files.upload_file`) instead of the English sentence. The interface follows the browser language until one is saved on the printer, and Simplified Chinese and Russian were added.

### Changed

- Print preparation is simplified to Side A/Side B plus one calibration option, automatically using the selected side's saved mesh, adaptive G-code probing or full-bed calibration, and requiring full-bed calibration when the side has no saved mesh.
- Fast thumbnail display by loading the image before the full metadata and caching metadata by storage, path, size and modification time.
- Every message, machine state and accessible name is translated in Italian, French, Chinese and Russian.

### Fixed

- File-manager, upload, Canvas, thumbnail and print handling; elapsed, remaining and total-layer status.
- First-run service realignment after LAN-code registration, removing the post-install manual reboot.
- Implausible G-code temperature metadata (such as 6211 °C) is rejected in the file preview.
- Vendor dispatcher crash risk and connection churn reduced (one supervised print completed without recurrence; the underlying cause remains unconfirmed).

### Removed

- Interface elements that showed nothing real: the printer-name field, the storage summary computed from the file list, the invented expert-console timeout, duplicated dashboard/job/control cards and the remaining demonstration values.

## [V4.1 / CC2 Control 1.1.25] - 2026-09-26

### Added

- The local CC2 Control service, and the V4.1/R8 reproducible builder snapshot.

### Fixed

- Persistent-storage startup ordering.

Earlier release details remain available through GitHub Releases, tags and Git history.

[Unreleased]: https://github.com/Neme77/centauri-carbon-2-community/compare/CC2-Control-1.2.30-V4.4.4-Universal...HEAD
[CC2 Control 1.2.23]: https://github.com/Neme77/centauri-carbon-2-community/releases/tag/CC2-Control-1.2.23-V4.4
[CC2 Control 1.2.19]: https://github.com/Neme77/centauri-carbon-2-community/releases/tag/CC2-Control-1.2.19-V4.4
[CC2 Control 1.2.18]: https://github.com/Neme77/centauri-carbon-2-community/releases/tag/CC2-Control-1.2.18-V4.4
[CC2 Control 1.2.17]: https://github.com/Neme77/centauri-carbon-2-community/releases/tag/CC2-Control-1.2.17-V4.4
[CC2 Control 1.2.16]: https://github.com/Neme77/centauri-carbon-2-community/releases/tag/CC2-Control-1.2.16-V4.4
[CC2 Control 1.2.15]: https://github.com/Neme77/centauri-carbon-2-community/releases/tag/CC2-Control-1.2.15-V4.4
[CC2 Control 1.2.14]: https://github.com/Neme77/centauri-carbon-2-community/releases/tag/CC2-Control-1.2.14-V4.4
[CC2 Control 1.2.11]: https://github.com/Neme77/centauri-carbon-2-community/releases/tag/CC2-Control-1.2.11-V4.4
[Community Firmware V4.4.1]: https://github.com/Neme77/centauri-carbon-2-community/releases/tag/V4.4.1
[CC2 Control 1.2.10]: https://github.com/Neme77/centauri-carbon-2-community/releases/tag/CC2-Control-1.2.10-V4.4
[Community Firmware V4.4 / CC2 Control 1.2.3]: https://github.com/Neme77/centauri-carbon-2-community/releases/tag/V4.4
[Community Firmware V4.3 / CC2 Control 1.2.0]: https://github.com/Neme77/centauri-carbon-2-community/releases/tag/V4.3
[V4.2 / CC2 Control 1.1.30]: https://github.com/Neme77/centauri-carbon-2-community/releases/tag/V4.2
[V4.1 / CC2 Control 1.1.25]: https://github.com/Neme77/centauri-carbon-2-community/releases/tag/V4.1

[CC2 Control 1.2.22]: https://github.com/Neme77/centauri-carbon-2-community/releases/tag/CC2-Control-1.2.22-V4.4

[Unreleased]: https://github.com/Neme77/centauri-carbon-2-community/compare/CC2-Control-1.2.30-V4.4.4-Universal...HEAD
[Community Firmware V4.4.4 / CC2 Control 1.2.29]: https://github.com/Neme77/centauri-carbon-2-community/releases/tag/V4.4.4
[CC2 Control 1.2.26]: https://github.com/Neme77/centauri-carbon-2-community/releases/tag/CC2-Control-1.2.26-V4.4


[CC2 Control 1.2.30]: https://github.com/Neme77/centauri-carbon-2-community/releases/tag/CC2-Control-1.2.30-V4.4.4-Universal
