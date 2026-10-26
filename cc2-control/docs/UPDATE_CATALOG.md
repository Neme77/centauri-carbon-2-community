# Signed update catalog release notes

Catalog schema 1 keeps the existing required `notes` string as the English
fallback, up to 2048 characters. A release may also carry an optional
`notes_i18n` object mapping supported language codes to plain-text descriptions:
`en`, `it`, `fr`, `pl`, `cs`, `hu`, `ru`, `zh`.

CC2 Control 1.2.11 displays notes only for a compatible version newer than the
installed version. The active UI language selects the description immediately,
without another catalog fetch. A missing, empty, non-string or oversized
localized value falls back to English, explicitly labelled in the UI. Notes
are rendered as escaped plain text, never HTML.

Keep the entire signed catalog under 8264 bytes (8192 bytes of JSON plus its
existing header/signature). Localization changes do not change URLs, version
selection, firmware compatibility, authentication or installer decisions.
Existing catalogs and verifier binaries remain compatible; the signature
still covers the entire catalog JSON, including localized notes.
