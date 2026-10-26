# Application backup/restore and selected-language startup

Status: implementation candidate; automated and real-printer qualification pending.
These settings improvements share PR #147. The backup suggestion and the report
of English text during startup came from **[DamiBFryta](https://www.reddit.com/user/DamiBFryta/)**.

## External application backup

Settings → Backup and restore downloads a JSON file to the user's computer.
The file includes UI preferences and Quick Actions, material presets, plate and
nozzle definitions with library measurements, and spool inventory/usage/history.
Export encodes the authoritative in-memory libraries, including spool usage not
yet due for a periodic disk save. It does not flush or modify those stores.

This is not a printer image. Passwords, LAN/MQTT/SSH configuration, network
settings, private keys, firmware/binaries, G-code and video are excluded. The
complete firmware `autosave.cfg` is never exported or restored. Official A/B
meshes not saved as measurements in the plate library are outside this scope.

Format v1 has `format: cc2-control-backup`, `schema: 1`, an eight-digit lowercase
`crc32`, and a `data` object containing model `CC2`, firmware family `4.4`, source
application version, creation timestamp and named `sections`. The CRC covers
the exact UTF-8 bytes of the `data` object. It detects corruption, not a trusted
author. Reformatting JSON changes the CRC; use the downloaded file unchanged.
Only user-owned, trusted backups should be restored.

The maximum file size is 512 KiB. Unknown formats/models/firmware families,
unsupported schemas or section names, duplicate/escaped keys, malformed JSON,
nonfinite values, invalid identifiers/references, oversized collections and
invalid mesh bounds/points are refused before changing application state.
The model/store parsers do not install the parsed data into globals.

## Preview and selective restore

Enter the printer LAN access code, choose the file, inspect the server-validated
preview and select categories. Selected categories replace their current data;
missing or unchecked categories are not erased or merged. A separate destructive
confirmation is required before restore. The LAN code is never put in the file
or browser storage. Standard same-origin and mutation-marker checks still apply.

The server accepts sequential chunks up to 8 KiB through its ordinary bounded,
nonblocking HTTP receive pool. One private process-memory upload of at most
512 KiB is allowed; a preview/upload expires after five minutes. There is no
blocking network upload loop or new worker thread. This intentionally avoids
persisting unvalidated uploads: only validated canonical data is staged on disk
at commit. No new runtime dependency or compression library is required.

Restore requires fresh MQTT and UDS evidence, connected idle/cold state, zero
heating targets, and no printing/paused job, calibration, console operation,
pending restart/mount, offset change or file/application transfer. Eligibility
is checked throughout upload and again at commit. Fingerprints detect edits to
selected current categories after preview; conflicts require a new preview.
Cancellation remains possible if printing starts after preview.

Restored calibration is data, not physical verification:

- Mounted/pending plate and selected nozzle state is cleared. No mesh load,
  Z-offset command, firmware slot rewrite, motion or printer reboot is sent.
  Check the actual plate/nozzle and measurements before explicitly mounting.
- Restored spool inventory and recorded history are retained. Tracking starts
  disabled; active job accounting, questions/timers and physical slot bindings
  are cleared. Previous assignments remain historical suggestions requiring
  confirmation. Import does not debit a spool or resume an interrupted job.
- Camera preference is preserved without starting a stream. Interface language,
  theme and presets refresh after a successful restore.

## Recoverable transaction

Fixed internally resolved application paths are used; no backup-supplied filename
or directory is accepted. Targets, staging and recovery files reject symlinks,
hard links, unsafe ownership and writable parent directories. Private recovery
storage is next to the preferences file in a `.restore` directory.

All selected previous data and canonical new data are written/fsynced before a
durable PREPARED journal is installed. New files are staged in their target
directories, then renamed and directory-synced. Success requires a durable
COMPLETE marker. A failed replacement rolls back every selected category. An
interrupted PREPARED operation is rolled back before loading stores or opening
the service on the next start. Unrecoverable/invalid recovery metadata refuses
startup rather than serving an unrecognized mixture. Previous plate/spool
recovery copies encode their current in-memory stores, including unsaved usage.
The latest completed recovery set is retained until the next restore begins.

This is a recoverable multi-file transaction, not a claim that several renames
are one filesystem-atomic operation. Tests inject write/rename failure and kill
a child after one replacement, then verify startup recovery. Hardware power-loss
qualification and printer resource/stability acceptance remain pending.

## Selected-language startup

The first meaningful application render awaits printer-saved preferences and its
selected dictionary. A neutral CC2 Control identity/spinner appears while these
bounded requests run. Both fetches have a 1.8-second abort timeout; failure reaches
a usable application and English fallback instead of an indefinite blank page.
The server's preferences win over browser hints, including another printer's
language setting. Failed language changes retain an already loaded dictionary.
Missing keys still fall back to bundled English.

Only the selected dictionary is downloaded, with validated bounded string data
and a shared in-flight request. Dictionary caching lasts only within the current
page/build; no persistent translation cache, service worker or offline HTML
application shell is introduced. Storage-disabled browsers are supported.

## API

- `GET /api/backup/export`: exact JSON bytes, `application/octet-stream` so a
  browser helper does not parse/re-stringify the checksum-protected data.
- `POST /api/backup/import/start`: decimal total size, LAN confirmation header;
  returns a random preview token.
- `POST /api/backup/import/chunk`: token, decimal offset header and ≤8192 raw bytes.
- `POST /api/backup/import/finish`: validate the complete file and return preview.
- `POST /api/backup/import/restore`: selected-category bitmask (1/2/4/8), token and
  LAN confirmation; preview and current eligibility/conflicts rechecked.
- `POST /api/backup/import/cancel`: discard private transient state.

## Validation gate

Candidate tests cover round-trip custom presets, uncalibrated/calibrated plates,
meshes/nozzles, unsaved spool values/history, corruption/duplicate/schema/refusal,
selective restore, chunk ordering/expiry, auth/idle/hot/stale/busy guards, symlink
refusal, failed writes/renames and interrupted-process recovery. Real HTTP checks
cover export integrity and responsiveness during an incomplete upload. Browser
checks exercise actual file selection, download, preview, checkboxes, confirmation,
cancellation/errors/mobile layout and first-render Italian/Polish/Hungarian with
delayed locale, disabled storage and failed/stalled requests.

Host/ARM/sanitizer/browser CI, exact UI/overlay/source snapshot pins and a
controlled cumulative printer trial must pass before merge/release. Trial
revisions do not advance the application release number.

## Italiano

In **Impostazioni → Backup e ripristino** si scaricano sul computer i dati
dell'applicazione; il file non contiene password o firmware. Per ripristinarlo,
inserire il codice LAN, scegliere il file, controllare l'anteprima e le categorie,
quindi confermare. Le categorie scelte sostituiscono i dati presenti.

Piatti, ugelli e misure importati vanno verificati sulla stampante reale prima
di montarli. Le bobine importate mantengono inventario e cronologia, con
tracciamento spento e abbinamenti fisici da riconfermare. Le mesh ufficiali A/B
che non sono state salvate nella libreria piatti non fanno parte del backup.
Le richieste di avvio della lingua sono limitate nel tempo e precedono la prima
visualizzazione dell'interfaccia. Qualificazione automatica e prova cumulativa
sulla stampante ancora da completare per questa revisione.
