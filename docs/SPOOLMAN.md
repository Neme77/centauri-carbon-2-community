# Optional Spoolman integration

The Spoolman integration was added by tharje (#208), got its own switch in #227 and was merged with the tested printer package in #231, where Neme77 validated it against a real Spoolman.

Settings → Integrations → **Enable Spoolman** controls the printer-wide integration. It starts off on a new installation. An existing configured trial without this flag keeps its previous enabled state on migration.

When enabled, Spools shows its Spoolman server settings, and spool forms show the Spoolman link field. Turning it off hides those controls and prevents new server requests. A request already sent may finish; its accounting result is still settled, while remote imports and metadata pulls are not applied while disabled.

URL, proxy credentials, spool links, pending usage and the independent local tracking setting are retained. Usage charged locally while paused stays queued; re-enabling uses the existing reconciliation rules rather than sending unknown-outcome usage twice. Removing or changing the server remains a separate action with #208's existing accounting behavior.

The flag persists across browser refreshes and application restarts. It is stored on the printer, not per browser. Exported backups never carry proxy credentials or an enabled integration; restoring a backup does not restart synchronization.

## What the sync does

- **Address.** The server is given as `http://IPv4-address[:port][/path]`, for example `http://192.0.2.5:7912`. A host name (`spoolman`, `spoolman.local`, `localhost`) is refused when it is saved, because the printer never resolves names; there is no TLS either, so `https://` is refused too.
- **Every two minutes** (and right after the integration is switched on) the printer reports the grams it has measured for each linked spool, then reads Spoolman's spool list and imports every Spoolman spool that has no local entry yet. Nothing has to be created by hand.
- **The list is read in pages** of 20 (`GET /api/v1/spool?sort=id:asc&limit=20&offset=…`, with `X-Total-Count` for the end). A page that is too big for the printer's buffer is halved; a spool that cannot be read at all is skipped and counted, the rest is still imported. Archived spools are not asked for, so they are never imported. At most as many spools are imported as the library has room for (128 in all).
- **A linked spool is refreshed from that same list**, without a request of its own. One that is not in the list (archived or deleted in Spoolman) is asked for by itself.
- **What is taken over.** Name, material, colour, maker (as the brand) and remaining weight. The colour is `color_hex` (`RRGGBB`, or `RRGGBBAA` of which only the RGB is kept); a multi-colour filament without `color_hex` takes the first of its `multi_color_hexes`. When an imported spool's Spoolman data has no `remaining_weight`, it is worked out as Spoolman does: the spool's own `initial_weight`, else the filament's `weight`, minus `used_weight`. The full-spool weight is the filament's `weight`; only when the filament has none, the spool's `initial_weight`, else what is left plus what was used; the empty-spool weight (`spool_weight` of the spool, else of the filament, else of the maker) is imported as the tare.
- **Spoolman is the source of truth** for a linked spool: a local change to its name, material, colour, brand or weight is overwritten by the next sync (within two minutes). A weight correction therefore belongs in Spoolman. The tare is only set at import.
- **Spools Spoolman has no weight for** (neither the spool nor its filament has a start weight) are not imported, and are counted in the status. A linked one keeps the weight it has here and still gets its other fields.
- **A material Spoolman does not give** (or one longer than 32 characters) is shown as PLA on import; the status counts them as `no_material`.
- **Time limits.** Each request has 15 seconds in all (the 5 second read timeout restarts with every byte a slow server sends), and one sync cycle has 90 seconds. Usage the cycle did not get to is kept for the next one, which starts where this one stopped.

## Status

`GET /api/spoolman` answers what the last cycle did: when, the result (`ok`, `unreachable`, `unauthorized` for 401, `forbidden` for 403, `not_found`, `server_error`, `refused`, `bad_reply`, `too_large`, `timeout`, `bad_url`, or `never` before the first cycle), the HTTP status if there was one, how many spools have grams waiting to be sent, what the list read (`pages`, `read`, `new`, `unreadable`, `no_weight`, `library_full`, `too_large`, `no_material`), how many linked spools Spoolman has archived (`archived_linked`; they are still synced) or does not know (`orphaned`, the Spoolman ids: usage for them waits until the spool is back). A line is written to the log when the result or the orphaned set changes, never one per cycle, and never the address or the login.
