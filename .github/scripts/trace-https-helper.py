"""Instrument a disposable CI working copy; never package this diagnostic build."""
from pathlib import Path
path=Path('cc2-control/updater/download.c')
text=path.read_text()
text=text.replace('if (mbedtls_ssl_handshake(&ssl) || mbedtls_ssl_get_verify_result(&ssl)) goto done;',
 'int handshake_result=mbedtls_ssl_handshake(&ssl);\n    fprintf(stderr,"TLS host=%s handshake=%d verify=%x\\n",host,handshake_result,(unsigned)mbedtls_ssl_get_verify_result(&ssl));\n    if (handshake_result || mbedtls_ssl_get_verify_result(&ssl)) goto done;')
text=text.replace('goto done;', '{ fprintf(stderr,"HTTPS trace line=%d host=%s result=%d\\n",__LINE__,host,ok); goto done; }')
text=text.replace('uint64_t size = 0; int have_size', 'fprintf(stderr,"HTTP host=%s status=%d\\n",host,status);\n    uint64_t size = 0; int have_size')
path.write_text(text)

