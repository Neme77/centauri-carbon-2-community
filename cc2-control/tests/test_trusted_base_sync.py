"""Keep the compiled-in update base pins (updater/trusted-base.h) in sync with
the installer compatibility record, so the two fail-closed guards (the on-device
verifier and the installer scripts) cannot drift apart."""
import json
import re
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
compat = json.loads((ROOT / 'installer/compatibility.json').read_text())
header = (ROOT / 'updater/trusted-base.h').read_text()

assert isinstance(compat['printer_sha256'], list) and compat['printer_sha256'], 'printer_sha256 must be a non-empty list'

array = re.search(r'cc2_update_printer_sha256\[\]\s*=\s*\{(.*?)\}', header, re.S)
assert array, 'printer array not found in trusted-base.h'
header_printers = re.findall(r'"([0-9a-f]{64})"', array.group(1))
assert header_printers == compat['printer_sha256'], (header_printers, compat['printer_sha256'])

daemon = re.search(r'CC2_UPDATE_DAEMON_SHA256\s+"([0-9a-f]{64})"', header)
assert daemon and daemon.group(1) == compat['daemon_sha256'], 'daemon pin drift between trusted-base.h and compatibility.json'

# The installer templates must consume the multi-base list placeholder, not the
# old single-value one.
for name in ('install-on-printer.sh', 'restore-on-printer.sh'):
    text = (ROOT / 'installer' / name).read_text()
    assert '@PRINTER_SHA256_LIST@' in text, name + ' missing @PRINTER_SHA256_LIST@'
    assert '@PRINTER_SHA256@' not in text, name + ' still uses the single-value @PRINTER_SHA256@'

print('PASS: trusted-base.h printer/daemon pins match compatibility.json; installer uses the multi-base list')
