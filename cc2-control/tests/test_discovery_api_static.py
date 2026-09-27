from pathlib import Path
source = (Path(__file__).resolve().parents[1] / 'src' / 'main.c').read_text()
required = [
    'CC2_DISCOVERY_API_VERSION 1',
    '/api/v1/system/info',
    '/api/system/capabilities',
    'cc2-community',
    'capabilities',
    'service_http_port = port;',
    'service_panda_port = panda_port;',
]
missing = [item for item in required if item not in source]
if missing:
    raise SystemExit('Missing discovery API markers: ' + ', '.join(missing))
print('PASS: discovery API v1 static markers present')
