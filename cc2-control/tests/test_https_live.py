"""CI-only live TLS validation. The production helper cannot download README."""
from pathlib import Path
import os
import sys
import subprocess
if os.environ.get('CC2_TEST_LIVE_HTTPS') != '1':
    print('SKIP: live HTTPS requires explicit CI network test')
    sys.exit(0)
ROOT=Path(__file__).resolve().parents[1]
tls=ROOT/'build/tls/mbedtls-3.6.7'
binary=ROOT/'build/https-live-test'
subprocess.run(['cc','-Os','-DCC2_HTTPS_TEST_DOCUMENT','-I'+str(tls/'include'),str(ROOT/'updater/download.c'),
    *map(str,[tls/'library/libmbedtls.a',tls/'library/libmbedx509.a',tls/'library/libmbedcrypto.a']),'-o',str(binary)],check=True)
url='https://raw.githubusercontent.com/Neme77/centauri-carbon-2-community/develop/README.md'
result=subprocess.run([str(binary),url,str(ROOT/'updater/ca-bundle.pem'),'524288'],capture_output=True,timeout=190)
assert result.returncode==0 and b'Centauri' in result.stdout,result.stderr.decode()
result=subprocess.run([str(binary),url,str(ROOT/'updater/trusted-key.json'),'524288'],capture_output=True,timeout=10)
assert result.returncode!=0 and not result.stdout
print('PASS: real GitHub HTTPS download with certificate verification; invalid trust store rejected')
