# File library and large uploads

The file browser follows cursor pages to make older G-code files available without
deleting newer files. Search, sorting and the existing 15-row controls operate on
the retrieved metadata. No file contents are loaded for listing, and no new
background polling is added.

## API and resource bounds

`GET /api/gcode-files` keeps its existing grouped response and first 128 files per
storage. Each group also contains `has_more`, `scan_truncated` and `next`.
When `has_more` is true, request the next window with:

```text
GET /api/gcode-files/page?storage=internal&before=1700000000&file=folder%2Fpart.gcode
```

Use `next.modified` verbatim as `before` (a decimal string), and URL-encode
`next.path` as `file`. Ordering is modification time descending, then path
case-insensitively, then exact path for case-only ties. A cursor never opens a
file and need not still exist. Listings are not snapshots: concurrent uploads,
deletes or timestamp changes may require Refresh.

The backend retains a fixed 128-entry window and the existing 192 KiB JSON buffer.
The existing traversal limit of 4096 directory entries and depth four remains.
When traversal is incomplete the UI warns rather than suggesting deletion.
Continuation requests rescan the bounded directory tree; the browser stops on a
repeated cursor or after 32 continuations per storage and aborts on navigation.
This is not an unlimited archive or server-side search.

Raw and Orca multipart uploads accept files up to 512 MiB. The existing 16 KiB
streaming buffer, one upload at a time, disk-space reserve, refusal to overwrite
and incomplete-file cleanup remain. Multipart staging still requires room for
two copies. The total upload deadline is 30 minutes, with the existing idle
receive timeout; the browser waits 30 minutes plus 30 seconds. Uploading never
starts a print automatically.

## Validation before release

Host integration tests cover older files, timestamp ties, case-only names, nested
encoded paths and malformed cursors. Upload tests check size boundaries and
450 MiB header admission followed by disconnect without allocating a large body.
Browser tests exercise 300 metadata entries and file-size validation with mocked
XHR transfer. They do not prove a real 512 MiB transfer on printer storage.

Before release, test a real 250–450 MiB upload, internal/USB free-space handling,
interruption cleanup, and opening/printing an older file on the printer.

Thanks to **DamiBFryta** for reporting the archive and large-file limitations and
providing real-world printing examples.
