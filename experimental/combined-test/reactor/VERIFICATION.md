# Runtime V2 — pre-integration verification

This is a new experimental module, not the withdrawn 505b3b3a executable and not a firmware release. Original file SHA-256 remains c21126e78a63ac9140e7347c56f363d5e513495c58c06fc17e8ef97846f3c0a3. It must be built with the user's existing GCC6.5 ARM/glibc2.23 toolchain, then checked natively before temporary activation.

## Design

Four entry points are redirected in private executable memory to a normal ARM shared object: callback invoke 0x469cd4, timer dispatch 0x46be24, PollReactor dispatch 0x46d520 and completion wait 0x4697b8. Eight-byte `ldr pc,[pc,#-4]` tail jumps add no intermediate stack frame. The replacement module has compiler-generated ARM exception tables. There is no new ELF segment in the vendor executable and no on-disk edit to its text.

The module uses original shared-pointer copy/destruction and JSON construction/call/destruction functions. It defines no substitute JSON ABI. Four original prologue fingerprints are checked before patching; the launcher and activation verify the entire original executable hash. All internal addresses apply ONLY to this exact hash. LD_PRELOAD is removed after construction so child processes do not inherit instrumentation.

## Observed ABI evidence

Original disassembly: shared copy 0x2cfbe0 and destroy 0x2cf490 operate on object/control pairs; getcurrent 0x46cf54 returns a nontrivial shared pointer using hidden result address. Callback JSON result call 0x4714c8 uses r0 output, r1 function and d0 time; JSON copy 0x2da7dc and destructor 0x2d0914 are reused. Completion wait originally saves r0 output, r1 this, d0 wake and r2 fallback. The replacement matches those registers. std::function<void(double)> call 0x3d566c, double timer function 0x472a34, poll PLT 0x2bc7e8 and monotonic PLT 0x2bc578 remain the original entry points.

Home crash core confirmed owner 0x2644098: timers vector +16, next timer double +32; normal callback vector +96, async +108; dispatch shared pair +144; map root +248; poll vector +264. Timer function occupies 16 bytes and wake is +16. Map node key is +16, shared value +20. Callback fields are owner +0, timer +8, function +16, completion +32. Completion result +8 and waiters +24. These offsets were read in the original disassembly/core; they are not inferred from the new official application layout.

## Completed local validation

- Exact replacement bridge logic compiled and passed with host mocked vendor entry calls, including successful/throwing normal and async callbacks, timer-vector mutation/cancellation, poll-vector reallocation and fd removal, completion failure/fallback, shared-control counts and JSON lifetime counters.
- UBSan passed this bridge test. Host pointer-width adaptation exists only for the fixture; production compile asserts ARM32.
- Additional 1024 suspension cycles, half throwing after resume, passed with a host coroutine shim. This is NOT execution of native libco in this local environment.
- Earlier user-run source-model ARM qualification passed with actual `/opt/lib/libcolib.so`, hash 14288a4a73c339b039dd89597618ebb05702f90d668b76f74019d5d15ce67403. It does not qualify this new bridge/module binding by itself.
- Shell script mock scenarios passed: success+manual restore; busy machine rejected before stop; wrong hash rejected before stop; missing activation marker triggers automatic original restoration.
- Shell syntax checks passed. Full host C++ syntax checked, including constructor code. PowerShell and ARM compilation are not available locally and remain user-side checks.

## Initial activation gate and limitations (historical)

Build checks ARM unwind sections and required function symbols, emits checksums. Remote transfer refuses an active prior module. Preflight runs the new bridge test with the production module loaded but disabled, using native libco for its coroutine cases. Activation then verifies cold/idle telemetry, keeps original bytes, restarts vendor services, checks loaded module and matching PID marker, and waits for fresh UDS. Failure rolls back; success keeps the experiment only until reboot or explicit cold/idle restore. No automatic timer interrupts a later homing.

At the time of the initial candidate, printing had not been tested. Unhandled original C++ errors can still terminate a coroutine/process; cleanup preserves propagation rather than synthesizing success. Kernel OOM/signals, request_log retention, video, MCU and global shutdown recovery are not fixed. First integrated trial is one idle Home All, with no print. Native compatibility and behavior remain experimental until that trial supplies evidence.

## Native qualification update, 2026-10-06
User ran the VFP qualification on native ARM/libco: O1 reproduced a disabled timer invocation on cycle 0; O0 passed 256 timer cycles and 1024 callback cycles, including 512 exceptions after suspension. Vendor calls remain mocked. Production module and bridge now compile O0. Integrated startup/homing had not yet been validated at this qualification stage.

## Later integration evidence

See `../VALIDATION.md` for user-reported persistent boot, successful Home All,
completed short and long prints, plus a collaborator comparison with reduced
UDS polling.
The original risks remain: native ABI compatibility is limited to the exact
fingerprinted executable, request_log is unbounded, and long-print reliability
and incremental benefit over reduced socket polling are not definitively established.
