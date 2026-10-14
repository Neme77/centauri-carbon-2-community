# Callback runtime: mechanism and validation

[Homepage](../README.md) · [Testing](TESTING.md) · [Historical V4.3 qualification](RELEASE_CANDIDATE.md)

## What the patch improves

The O0 reactor runtime addresses references retained after webhook callbacks complete. It removes the callback from its owner queues and deregisters its timer while preserving the reference needed for execution. Cleanup releases the associated references and JSON results on both the normal and exception paths. Completion wait handling also removes finished waiters.

This targets a specific source of retained objects, rather than periodically clearing Linux caches or stopping the camera. Persistent UDS telemetry, contributed by beanbo, separately reduces repeated socket requests.

## Port to the new firmware base

V4.4 uses official base **02.01.01.07**. Its runtime targets the fingerprinted printer executable and maps the same four reactor operations — callback invocation, timer checking, poll dispatch and completion waiting — to that executable. The port preserves the qualified O0 approach and pins the native coroutine library. The source and address mapping are recorded in the [port manifest](../experimental/firmware-port/reactor/port-manifest.json) and [runtime source](../experimental/firmware-port/reactor/reactor_module.cpp).

O0 remains required: earlier native coroutine qualification reproduced a regression with O1. The runtime is specific to the verified firmware inputs and must not be transferred to another base without qualification.

## Positive validation evidence

| Test or observation | Scope and result |
| --- | --- |
| Native coroutine/bridge qualification | The earlier O0 qualification recorded 256 timer suspension cycles and 1,024 bridge cycles, including 512 exceptions after suspension. See the [qualification record](../experimental/combined-test/VALIDATION.md). |
| Automated release-preparation checks | CC2 host tests, ARM build/smoke checks, both sanitizer configurations, UI checks, builder tests and reactor fixtures passed on [PR111](https://github.com/Neme77/centauri-carbon-2-community/pull/111). |
| Printer activation | Maintainer diagnostics showed the runtime mapped into the printer process, an activation marker matching its PID, and runtime state updates with completed callbacks. |
| Community long-print campaign | The maintainer and beta testers reported 40-hour prints and more than 20 confirmed long tests without reported freezes; smaller reported issues were corrected. |
| Memory observations | Available memory recovered after activity during the beta cycle. Individual readings varied with printing, camera use and other system activity. |

The long-print reports cover the wider beta campaign; they do not mean every test used the exact final firmware hash. They are encouraging evidence of the integrated system, not a controlled A/B measurement proving a fixed number of megabytes recovered by this patch alone.

## Practical scope

The patch does not bound the vendor request log, release all video/kernel buffers or prove that every possible OOM/803 cause is eliminated. Reassess this runtime when matching vendor sources or a qualified official correction become available.

The main runtime diagnostics are `/opt/usr/cc2-reactor-runtime-v2/activation.marker` and `reactor-state.txt`. Matching the live PID and observing state updates help establish activation; callback, exception, queue and timer counters describe the sampled reactor, not total printer RAM.
