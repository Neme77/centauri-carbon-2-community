# File dates and printer clock synchronization

CC2 Control reads each G-code's filesystem modification timestamp. It does not
derive the upload date from the file name or G-code comments. The browser shows
valid timestamps in the viewing device's locale and timezone.

Files written before the printer clock synchronizes can retain a timestamp near
1 January 1970, even after the clock becomes correct. These dates cannot be
reconstructed reliably. Internet filtering can prevent external time servers
from responding; a running NTP process alone does not prove successful sync.
Neither an ELEGOO cloud outage nor a cloud dependency has been established as
the cause of this report.

The Files page treats missing, non-finite, unrepresentable and pre-2000 timestamps
as unavailable. This is a conservative display rule for this printer, not proof
that a timestamp after that boundary is accurate. In Newest first, files with
usable dates precede files with unavailable dates; the latter sort by name.
Name and size sorting continue to use their own criteria. Backend list limits
and raw API timestamps are unchanged.

The workaround does not rewrite files, set the system clock, add a daemon or
repair existing metadata. Correct clock synchronization is needed for future
uploads. A read-only check on the reporting firmware showed an existing ntpd
using ntp5.aliyun.com and 0.openwrt.pool.ntp.org. Networks that block external
access should provide an appropriate reachable time source, for example a local
NTP server, or allow the configured NTP traffic. Check actual configuration
before changing it; do not run a second competing synchronization service.

Browser regression tests cover epoch-like/missing values, boundary dates,
unchanged local formatting, sorting controls, search, refresh and mobile layout.
Real-printer UI confirmation remains separate from automated tests.

Builder validation uses the synchronized canonical source archive and unchanged checksum guards.
