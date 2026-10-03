# Runtime edge-case corrections

This change addresses the uploaded runtime review while keeping the existing
memory limits and avoiding automatic retries of print commands.

- Z-offset confirmation expires after five seconds using a monotonic clock.
  The pending/session state is cleared even if UDS is unavailable; subsequent
  adjustments still require a fresh firmware value. The UI explains the timeout.
  A successful new command clears that message. Limits remain +/-0.50 mm from
  the new confirmed reference.
- Raw uploads require space for the payload plus a 16 MiB reserve. Orca multipart
  uploads require space for both spool and final copy plus the same reserve.
  Checks run before accepting the body/100 Continue and again in the workers;
  errors return 507. These checks do not reserve space against other applications,
  so write/finalization errors must still be handled normally.
- Upload reception uses a five-minute total monotonic deadline in addition to
  the existing twenty-second idle limit. Timeouts remove temporary files and
  release the upload slot; an expired receive returns 408.
- The HTTP receive pool remains at eight slots. Increasing to sixteen would
  allocate another roughly 96 KiB of request buffers. Saturation now returns a
  best-effort nonblocking 503 with Retry-After: 2, followed by connection close.
- Oversized MQTT QoS 0 publishes are drained through the existing 16 KiB buffer
  without reconnecting. This is the QoS requested by our subscription. Snapshots
  exceeding their existing 8/12 KiB caches are rejected instead of truncated,
  preserving the last complete cached snapshot. Unsupported oversized control
  packets/QoS still cause disconnect rather than bypassing protocol handling.
  `/api/health` exposes `mqtt_oversized_packets` and `mqtt_oversized_snapshots`.
  Printer tests must determine whether real Canvas/info snapshots exceed those
  limits; these changes do not add support for arbitrarily large snapshots.
- Motors off and Fans off follow the backend Idle requirement, including pause.
- Restoring preferences saves Quick Actions before changing theme or language;
  a rejected Quick Actions save leaves the current appearance unchanged.
- Multipart name= is matched at an attribute boundary, allowing filename= to
  precede name= without being confused with the field name.

The existing Orca pending-confirmation check already runs before publication.
A regression confirms that a second upload-and-print is rejected without leaving
its file behind. Only the serialized upload worker creates pending confirmations;
the later publication check remains a defensive guard. Failed print preparation
is deliberately not retried automatically, to avoid issuing a duplicate start.
The operator can retry from Files after resolving the error.

Flow and speed stay in Dashboard as previously agreed. Host/browser tests and
an ARM build accompany this change; real-printer storage, Z-offset and MQTT
validation remain required before merge.
