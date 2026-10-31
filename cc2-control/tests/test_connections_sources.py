"""Keep the qualified standalone helper sources and the app runtime copies in sync."""
from pathlib import Path
import hashlib
import json

app = Path(__file__).resolve().parents[1]
component = app.parent / 'experimental/network-connections'
if not component.exists():
    # Standalone source snapshots carry the app headers; full helper sources are
    # supplied with the corresponding qualified runtime archive.
    print('PASS: standalone app snapshot; Connections helper source bundle is separate')
else:
    for path in component.glob('*.h'):
        target = app / ('src/connections_http.h' if path.name == 'http.h' else 'src/connections/' + path.name)
        assert target.read_bytes() == path.read_bytes(), path.name
    status = (component / 'status.c').read_text().split('\n#ifndef CC2_NETWORK_STATUS_LIBRARY\n', 1)[0].rstrip() + '\n'
    assert (app / 'src/network_status.c').read_text() == status
    lock = json.loads((component / 'runtime.json').read_text())
    assert lock['helper_sha256'] in (app / 'src/connections/pin.h').read_text()
    assert lock['qualified_profile']['usb_product'] == '8153'
    assert '8139' not in json.dumps(lock['qualified_profile'])
    print('PASS: helper/app source copies and exact RTL8153 runtime profile agree')
