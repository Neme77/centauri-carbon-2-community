# Native network compatibility trial

Temporary, opt-in research for the qualified CC2 ARM32 camera and printer.
This is not a firmware release or a validated hardware feature.

The native camera reads wlan0 flags/carrier/address and stops its streamer
when that interface loses connectivity. The native printer asks specifically
for wlan0 in its network and IP-change monitor. A working wired IP/default
route alone therefore does not supply native cloud compatibility.

This module wraps only the qualified methods. It substitutes the real wired
interface name in the native monitors, reads the real wired camera state and
returns the real selected local IP. It never forces the Internet health probes
to succeed. No on-disk vendor executable is patched. Existing camera reap and
recovery hooks and the reactor hooks are outside its patch sites.

The root-owned, owner-only, regular file
`/tmp/cc2-network-compat-r1.mode` selects `wifi\n` or `ethernet\n`. Missing,
malformed or insecure files preserve native behavior. This file is a control
for the supervised test only; it is not an authenticated HTTP interface.
An eventual automatic selector must have independently tested routing/DNS,
quality thresholds, hysteresis and native WAN-blocking behavior. This first
module does not implement signal-driven automatic selection.

Host/ARM tests check real std::string hidden-return/SSO/destruction ABI and
actual ARM entry trampolines with synthetic functions at the qualified native
addresses. The synthetic functions are not vendor firmware. Printer hardware,
Matrix, RTC, printing and coexistence with the installed native patches require
separate validation. No installer or service restart runs in the build job.

All executable qualification and instruction fingerprints remain mandatory.
Do not load this experimental module into an unqualified program or start it
while a printer operation is active.
