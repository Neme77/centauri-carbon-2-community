# Experimental reactor/callback runtime (O0)

The original ELEGOO `elegoo_printer` source retains completed reactor callbacks:
[`register_callback`](https://github.com/elegooofficial/CentauriCarbon2/blob/5a2ea7fc03e707552701b1a69f463699cbd39230/elegoo/reactor.cpp#L286-L293)
appends each callback to a vector that is never pruned. A webhooks request's
closure retains its `WebRequest` and connection. Separately,
[`request_log`](https://github.com/elegooofficial/CentauriCarbon2/blob/5a2ea7fc03e707552701b1a69f463699cbd39230/elegoo/webhooks.cpp#L104-L114)
grows without a bound. Beanbo documented source analysis and real-printer
measurements in [ELEGOO issue #12](https://github.com/elegooofficial/CentauriCarbon2/issues/12)
and reduced the requests that feed the leak in community PR #88.

This runtime experiment redirects callback, timer, poll and completion-wait
entry points in private process memory. It releases finished callbacks and
handles timer/FD mutation and exception cleanup while preserving the vendor
executable on disk. It is compiled at **O0**: native-libco qualification reproduced
a floating-point state regression at O1. It does **not** bound request_log or
claim to resolve every OOM/803 failure.

Compatibility is restricted to the original executable and libco SHA-256 values
in `LEGGIMI.txt`, plus instruction fingerprints in the module. The installer
checks these, requires cold/idle telemetry, runs native bridge preflight, keeps
machine-specific backups, checks activation and fresh UDS, and rolls back on
failure. Persistent startup edits only the normal command in the real vendor
`/opt/bin/run_printer.sh`, preserving `LD_BIND_NOW=1` and arguments.

This is a draft experiment, not enabled by the standard firmware builder. The
combined test installer builds the current branch's CC2 plus the runtime;
it does not add separate PID, plate-library or file-analysis work.
No vendor executable, shared library, private key or prebuilt module is included.
Native builds require the owner's GCC 6.5.0/glibc 2.23 toolchain and a local copy
of the exact printer libco under `reactor/vendor-link/`. Do not commit that copy.

Build from the repository root on Windows/WSL with:

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\experimental\combined-test\Build-ARM.ps1
```

Read `LEGGIMI.txt` before installing. `VALIDATION.md` and `reactor/VERIFICATION.md`
separate host fixture checks, native ARM qualification and real-printer reports.
Long-duration validation and comparison against PR #88 alone remain pending.
