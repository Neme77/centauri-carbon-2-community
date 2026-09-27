# Repository working rules

- Treat `cc2-control/` and `builder/current/` as the canonical development
  sources.
- Use stable filenames and paths. Put release numbers in tags and the changelog.
- Keep changes small enough to review and pair behavioural changes with tests.
- Never commit LAN codes, local configuration, private keys, vendor firmware or
  generated build output.
- Preserve third-party notices and do not imply ownership of vendor components.
- Run the relevant tests before committing and record real-printer validation
  separately from host-side tests.
- Do not rewrite public history for cosmetic cleanup.
