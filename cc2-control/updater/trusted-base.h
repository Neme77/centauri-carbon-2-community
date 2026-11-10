/* Accepted vendor printer bases: ELEGOO 02.01.01.07 and 02.01.01.13. The
 * application payload is base-independent, so a single signed update is
 * compatible with either patched printer; the daemon is byte-identical across
 * both bases. Keep in sync with installer/compatibility.json (enforced by
 * tests/test_trusted_base_sync.py). */
static const char *const cc2_update_printer_sha256[] = {
    "19712b5943b10bb8cb6aab9a4ed498ee962b28538b23c91b7ed003eb9e590745",
    "688b7bf8a27762d56b35d9738ea5b0c3d45580b402e122b0f77ee49dab1a8cfd",
};
#define CC2_UPDATE_DAEMON_SHA256 "83433b14e49d1e8aa9523282ead03618378573f4b66de1ad8f7f3c444b11aada"
