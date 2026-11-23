# Panel enhancements under validation

## Storage and lists

The file endpoint reports filesystem capacity, available bytes and free bytes for internal storage. USB space is unavailable when the configured USB path is absent or is an ordinary directory on the internal filesystem. Values are read during the existing file-list request; page navigation does not query the printer.

File Manager and History show 15 items per page. Filtering and sorting reset the File Manager page. Deleting the last item on the last page clamps the page to the remaining results. File selection is retained across pages; Select all selects only the current page. Clear History still applies to all completed tasks, with the existing confirmation.

## Live view

Settings → Appearance stores camera_persistent on the printer, default false. When enabled, the existing full-size camera card keeps the same image and stream across menu navigation, including a hidden tab or minimized browser. It appears in the normal Dashboard/Job camera position and is parked out of view on other pages; no floating window is created. Live view still requires an explicit Start. Pause, disabling the option, closing/leaving the page and a confirmed viewer takeover stop the request. A reload restores the option but never automatically starts video. Browser/OS process suspension or loss of network can still interrupt transmission. Persistent live view retains the resources of an active camera; it is not a RAM optimization.

The normal per-page stream remains unchanged with the option disabled. The live panel and separate camera window retain the existing one-viewer ownership rule.

The Dashboard camera card has a Mini player button, a toggle (pressed while the window is open; pressing it again closes the window and ends its stream): the same live view in a small window above every page, which stays open across menu changes. A bar on top carries the size handle in the top left corner, the grip, pause, the zoom buttons and close; the whole bar and a thin frame at the left, right and bottom edge move the window. The Job page's camera, while it is on, has a Mini player button too, which hands its picture over. The mini player is a viewer like the card and the camera window, so it claims the camera and closes itself, without a notice, when another viewer starts live view; that other viewer keeps its stream. In one browser the old picture ends before the new one starts (a second tab is asked over a BroadcastChannel and is waited for at most one second); between two devices the previous viewer still stops on its next printer-state poll, so both can stream for up to about one poll interval. A Camera button on the Job page that was left on does not start a stream by itself while the mini player is open. Its size is 16:9, 240-640 px and at most half the window (160 px to the window width minus 24 px on a phone). Nothing about it is remembered: closing it by hand (X or the button in the card), another viewer taking the camera and a reload all end with the standard size at the bottom right on the next opening, so a reload never starts video (a value an earlier version stored under `cc2-miniplayer` is removed once). It can be put anywhere in the browser window but keeps out of the emergency stop and, on a phone, the menu button in the header; Escape does not close it. Zoom up to 2x is applied to the picture in the browser; the stream from the camera service and its single connection are unchanged. A hidden tab pauses the mini player unless Keep active is on; when the tab is shown again the mini player first checks who has the camera and closes without a notice if another viewer has it, instead of taking the camera back. While it streams the printer state is read every 1.5 s to notice a takeover, also on pages that do not read it that often.

## Signed application updates under validation

Settings → Updates now prepares signed local or GitHub-distributed application
packages. See [English instructions](APPLICATION_UPDATES.md) and
[Italian instructions](APPLICATION_UPDATES.it.md). The standalone TLS helper
does not depend on the printer's curl. The stable and experimental Canvas UDS
channels remain separate. Printer qualification is pending; firmware updating
through the original display path remains a separate future task.

## Update notification in the header

A bell beside Emergency Stop indicates a newer CC2 Control version compatible
with the installed firmware and the selected update channel. Open it to see the
version, then choose **Open updates** to reach Settings → Updates directly.
Download and installation still require explicit confirmation and the LAN code;
the notification never installs packages or changes printer firmware.

While the main panel is open, it checks the small signed GitHub catalog on
startup and reuses a successful check for six hours. The shared scheduler sleeps
in hidden tabs. Failed automatic checks stay silent and retry after at least
fifteen minutes; navigating between pages does not bypass this delay. Manual
**Check for updates** remains available in Settings. The existing backend
signature verifier validates both manual and automatic catalogs.

The emergency stop is slightly narrower on desktop; compact headers retain
44-pixel action targets and accessible labels. Its one-second hold is unchanged.
Validation includes browser interactions at widths from 320 to 1920 pixels,
channel/firmware/version filtering, shared cache, silent network/signature
failures, direct tab navigation and the emergency hold. Printer validation of
this notification is separate from these browser checks.
