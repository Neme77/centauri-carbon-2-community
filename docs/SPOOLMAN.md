# Optional Spoolman integration

This follow-up depends on tharje's #208 integration. It is not a release and awaits the contributor's real-server validation.

Settings → Integrations → **Enable Spoolman** controls the printer-wide integration. It starts off on a new installation. An existing configured trial without this flag keeps its previous enabled state on migration.

When enabled, Spools shows its Spoolman server settings, and spool forms show the Spoolman link field. Turning it off hides those controls and prevents new server requests. A request already sent may finish; its accounting result is still settled, while remote imports and metadata pulls are not applied while disabled.

URL, proxy credentials, spool links, pending usage and the independent local tracking setting are retained. Usage charged locally while paused stays queued; re-enabling uses the existing reconciliation rules rather than sending unknown-outcome usage twice. Removing or changing the server remains a separate action with #208's existing accounting behavior.

The flag persists across browser refreshes and application restarts. It is stored on the printer, not per browser. Exported backups never carry proxy credentials or an enabled integration; restoring a backup does not restart synchronization.

Validation must include the physical-printer cumulative package and the contributor's real Spoolman instance before release.
