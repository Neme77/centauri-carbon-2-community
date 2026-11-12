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
    # The stock13 helper is a separately qualified native profile. Preserve the
    # exact app snapshot instead of copying manager-only native paths into it.
    # Unknown differences still fail: exceptions are committed reviewed pairs,
    # not generated/accepted from the current files during this test.
    profile = json.loads((component / 'source-variants.json').read_text())
    assert profile['schema'] == 1 and profile['base'] == '02.01.01.13'
    variants = profile['variants']
    observed = set()
    for path in component.glob('*.h'):
        target = app / ('src/connections_http.h' if path.name == 'http.h' else 'src/connections/' + path.name)
        if target.read_bytes() != path.read_bytes():
            entry = variants[path.name]
            assert entry['reason'], path.name
            assert hashlib.sha256(path.read_bytes()).hexdigest() == entry['helper_sha256'], path.name
            assert hashlib.sha256(target.read_bytes()).hexdigest() == entry['app_sha256'], path.name
            observed.add(path.name)
    assert observed == set(variants), 'Remove stale variants when source copies converge'
    status = (component / 'status.c').read_text().split('\n#ifndef CC2_NETWORK_STATUS_LIBRARY\n', 1)[0].rstrip() + '\n'
    assert (app / 'src/network_status.c').read_text() == status
    lock = json.loads((component / 'runtime.json').read_text())
    assert lock['helper_sha256'] in (app / 'src/connections/pin.h').read_text()
    assert lock['lan_preview']['helper_sha256'] in (app / 'src/connections/pin.h').read_text()
    assert lock['qualified_profile']['usb_product'] == '8153'
    assert '8139' not in json.dumps(lock['qualified_profile'])
    print('PASS: shared helper/app sources, exact qualified stock13 variants and RTL8153 runtime profile agree')
