"""No printer curl dependency; reject all unapproved sources before network I/O."""
from pathlib import Path
import subprocess
ROOT=Path(__file__).resolve().parents[1]
binary=ROOT/'build/cc2-https-download'
ca=ROOT/'updater/ca-bundle.pem'
for url in ('http://github.com/Neme77/centauri-carbon-2-community/releases/download/x/x.cc2update',
    'https://example.com/payload','https://127.0.0.1/payload','https://github.com@evil.test/payload',
    'https://github.com/another/repository/releases/download/x/payload',
    'https://github.com/Neme77/centauri-carbon-2-community/releases/download/x/payload\r\nInjected:yes',
    'https://raw.githubusercontent.com/another/repository/main/code'):
    result=subprocess.run([str(binary),url,str(ca),'8192'],capture_output=True,timeout=4)
    assert result.returncode!=0 and not result.stdout,url
print('PASS: HTTPS only, fixed repository/hosts and HTTP header injection rejection')
