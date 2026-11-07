"""Conservative optional publication selection; no hardware permission here."""
from pathlib import Path
import json
import subprocess
import sys
import tempfile

ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'installer'))
from native_delivery import CANVAS_SELECTION

cases=[
    ('{"available":true,"telemetry":{"canvas_list":[{"connected":1}]}}',0,0),
    ('{"available":false,"slots":[]}',0,2),
    ('{"available":true,"telemetry":{"canvas_list":[{"connected":0}]}}',0,2),
    ('{"available":true,"telemetry":{"canvas_list":[{"connected":1},{"connected":1}]}}',0,2),
    ('{ "available" : true, "telemetry" : {"connected" : 1 }}',0,0),
    ('{"available":true,"slots":[]}',0,2),
    ('{"available":false}',0,2),
    ('{"available":true,"telemetry":{"connected":1}}',1,1),
    ('',0,1),
    ('{"available":true',0,1),
    ('{"other":{"available":true,"connected":1}}',0,1),
    ('{"available":"true","connected":1}',0,1),
    (json.dumps({"available":True,"telemetry":{"name":'escaped "connected":1, value'}},separators=(',',':')),0,2),
]
count=0
with tempfile.TemporaryDirectory(prefix='cc2-canvas-install-') as temp:
    root=Path(temp)
    for shell in [['dash'],['busybox','sh']]:
        for body,status,expected in cases:
            # Only trusted loopback text is returned by the HTTP fixture.
            (root/'response').write_text(body)
            script=root/'test.sh'
            script.write_text('set -eu\ninstaller_http_get(){ cat "'+str(root/'response')+'";return '+str(status)+'; }\n'+CANVAS_SELECTION+'\nresult=0;native_canvas_available || result=$?\nexit "$result"\n')
            result=subprocess.run([*shell,script],capture_output=True,text=True,timeout=5)
            assert result.returncode==expected,(shell,body,result.returncode,result.stderr)
            count+=1
print(f'PASS: {count} optional Canvas selection dash/BusyBox cases; unavailable/disconnected/ambiguous defers, HTTP/shape errors refuse, no device action')
