# HTTPS download component

Mbed TLS 3.6.7, official release archive:
https://github.com/Mbed-TLS/mbedtls/releases/tag/mbedtls-3.6.7

SHA256: a7e8bcbec0e6f761b4af24f25677626b35f762f68eef79c08677a363212d11f6

The unmodified upstream archive includes its license and provenance. It is
extracted only under `build/` and statically linked into a short-lived download
helper, never into the resident CC2 Control process. Full license:
`mbedtls-3.6.7.tar.LICENSE`.

The CA bundle is Mozilla's public root certificate data as extracted by curl:
https://curl.se/docs/caextract.html (2026-09-25 snapshot).
Bundle SHA256: a41b5d356aea97a529fe27e0f7316d2f9d946d75927476cf9cf1b90637d00505
Its source and license references are preserved in `updater/ca-bundle.pem`.

No private release signing keys are included.
