"""Exercise the actual downloader response parser, with transport-only stubs."""
from pathlib import Path
import subprocess
ROOT=Path(__file__).resolve().parents[1]
tls=ROOT/'build/tls/mbedtls-3.6.7'
subprocess.run(['make','build/cc2-https-download','CROSS=','CC=gcc','EXTRA_CFLAGS=-Werror'],cwd=ROOT,check=True,stdout=subprocess.DEVNULL)
binary=ROOT/'build/test-https-parser'
subprocess.run(['gcc','-std=c11','-Wall','-Wextra','-Werror','-I'+str(tls/'include'),str(ROOT/'tests/test_https_parser.c'),
    *map(str,[tls/'library/libmbedtls.a',tls/'library/libmbedx509.a',tls/'library/libmbedcrypto.a']),'-o',str(binary)],check=True)
subprocess.run([str(binary),str(ROOT/'updater/ca-bundle.pem')],check=True)
