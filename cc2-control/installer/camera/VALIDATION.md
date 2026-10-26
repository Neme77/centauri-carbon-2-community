# Camera component integration validation

The complete 1.2.19 update requires a full manual printer reboot while idle after installation. Neither the application installer nor camera manager reboots the printer or restarts ai_camera.

The R2 native files are reused byte for byte. They were observed through seven hours of printing with AI options enabled and streaming working. This does not qualify individual AI detections or expired RTC sessions.

The combined installer is a new path and requires its own printer acceptance test. Tests exercise native and previous one-shot startup profiles, exact rollback, loader/manifest/firmware refusal, disabled activation and repeated boot. Existing personal R2 installations are preserved after fixed script/runtime and derived-startup fingerprint verification. A rolled-back personal directory is dormant and must not block a recognized original startup.

The transaction test executes the actual generated octal-embedded archive extraction and application installer with isolated service/filesystem doubles. Cases include preflight refusal, application health failure, component failure before and after startup commit, exact manual restore and refusal to overwrite a later foreign startup edit. Original G-code, clock settings, printer executable and callback patch remain unchanged.

These checks are independent of the earlier temporary camera-only restart qualification. Do not publish a stable catalog entry before the combined installer has been accepted on the printer.
