# Camera client-thread reclamation — trial R2

Experimental ai_camera-only trial for Community Firmware V4.4/V4.4.1, official base 02.01.01.07. This does not update CC2 Control.

Required camera SHA256: ec5633b17de329d5b18c3f8d8de6ebf389391a04193536d976ba167022d07b99. Other executables are refused.

**tharje** reproduced retained client stacks followed by an uncaught std::system_error and SIGABRT in issue #160. This trial replaces one client-thread creation call: pthread_tryjoin_np reclaims genuinely completed threads, while creation failures close/remove the incoming connection. The retained vector stays compatible with native stop/join/clear. No detached workers, extra reaper thread or polling are added.

The native four-active-client limit is unchanged. The trial retains at most eight joinable workers to cover the brief gap between socket removal and actual thread exit. Further incoming clients are refused at that bound.

**Manual camera restart:** the original executable initializes LAN streaming paused. The trial launcher explicitly enables a second small hook which initializes that flag to true once for the fresh object. Later vendor pause/resume controls remain operative. This startup difference is separate from the reclamation fix and requires hardware validation.

## First phase

1. Printer connected and idle, no print/update running. Pause live viewing and close other camera viewers.
2. Extract only to /tmp/cc2-camera-reap-r2.
3. Run: sh /tmp/cc2-camera-reap-r2/trial.sh check
4. Share the result before activation. Check does not restart any process.

## Activation after checking

Run: sh /tmp/cc2-camera-reap-r2/trial.sh start

Only ai_camera is restarted. The camera executable on disk, printer services, callback patch and Wi-Fi settings are unchanged. The library patches two calls in the new process's memory after checking the full executable hash and native instructions.

An activation marker does not qualify video, Matrix/RTC or AI. Open the stream normally and report the result, then run:

sh /tmp/cc2-camera-reap-r2/trial.sh status

Compare spawned/reaped/retained and VmSize across a few ordinary open/close operations. Do not start with a reconnect stress loop. This is not qualified for long prints or unattended use.

## Rollback

While idle: sh /tmp/cc2-camera-reap-r2/trial.sh rollback-reboot

Alternatively reboot the idle printer normally. No persistent startup configuration is changed, so the trial does not return after reboot. If trial startup fails, retain ai_camera-trial.log and reboot while idle; do not retry automatically.

The package includes only original patch sources and binaries built from them, no vendor firmware/camera executable, credentials or private keys. Host/emulated ARM tests do not replace hardware acceptance. Slow Wi-Fi transmission remains a separate investigation.

## R2

R2 fixes the startup hook from R1: the early initialization was overwritten by a final native store to false. The opt-in hook now replaces that final store. Offline execution of the full native constructor reproduces the R1 failure and leaves the flag enabled with R2; camera/SDK behavior still requires printer validation.
