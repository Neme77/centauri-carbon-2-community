"""Request the existing vendor image/font synchronization on the next OTA boot.

Only the two selected A/B update branches change. Kernel/boot-resource images
and the vendor printer startup script remain untouched.
"""
import hashlib

STOCK_SHA256='6598586ada5e7e9256316c63b801579e65e797d2a77e13ab360f09b3581ae2fb'
ANCHOR=b'            bootenv: (\n'
ENTRY=(b'                {\n'
       b'                    name = "ota_flag";\n'
       b'                    value = "true";\n'
       b'                },\n')


def patch(data):
    if hashlib.sha256(data).hexdigest()!=STOCK_SHA256:
        raise RuntimeError('Unsupported official SWU description')
    if data.count(ANCHOR)!=2 or b'"ota_flag"' in data:
        raise RuntimeError('Unexpected native OTA resource boot branches')
    result=data.replace(ANCHOR,ANCHOR+ENTRY)
    if result.replace(ANCHOR+ENTRY,ANCHOR)!=data or result.count(b'"ota_flag"')!=2:
        raise RuntimeError('OTA resource synchronization changed other boot state')
    return result
