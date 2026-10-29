"""Fetch the pinned test-only dependency; never add it to the printer UI."""
import base64
import hashlib
import io
import json
from pathlib import Path
import tarfile
import urllib.request

root=Path(__file__).resolve().parent
pin=json.loads((root/'axe-pin.json').read_text(encoding='utf-8'))
with urllib.request.urlopen(pin['url'],timeout=30) as response:blob=response.read()
assert pin['integrity']=='sha512-'+base64.b64encode(hashlib.sha512(blob).digest()).decode()
out=root/'output/axe'
out.mkdir(parents=True,exist_ok=True)
with tarfile.open(fileobj=io.BytesIO(blob),mode='r:gz') as archive:
    for name in ('axe.min.js','LICENSE'):(out/name).write_bytes(archive.extractfile('package/'+name).read())
print('Test-only axe-core verified:',pin['version'])
