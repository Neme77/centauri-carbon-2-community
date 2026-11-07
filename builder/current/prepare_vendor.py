#!/usr/bin/env python3
"""Recover the qualified libco locally from the user's authenticated official OTA."""
from pathlib import Path
import importlib.util
import shutil
import subprocess
import tempfile

HERE=Path(__file__).resolve().parent
REPO=HERE.parents[1]
spec=importlib.util.spec_from_file_location('vendor_inputs',HERE/'core/firmware_builder.py')
builder=importlib.util.module_from_spec(spec);spec.loader.exec_module(builder)
LIBCO_SHA=builder.QUALIFIED_PROFILE['sha256']['libcolib']

def main():
    target=REPO/'experimental/firmware-port/reactor/vendor-link/libcolib.so'
    if target.is_file():
        builder.reqhash(target,LIBCO_SHA,'local native libco');return
    aes=builder.req(HERE/'keys/cc2_aes_key_v1.bin','local AES input')
    builder.reqhash(aes,builder.EXPECTED['aes_key'],'AES input')
    # libco belongs to the qualified reactor port, so it is recovered from the qualified base only.
    official=builder.req(HERE/'original_firmware'/builder.QUALIFIED_PROFILE['stock_package'],'official OTA')
    un=builder.find_squash_tool(None,'unsquashfs')
    with tempfile.TemporaryDirectory(prefix='cc2-vendor-input-') as temporary:
        work=Path(temporary)
        swu=builder.decode_stock_package(official,aes.read_bytes(),work)
        with swu.open('rb') as source:
            subprocess.run(['cpio','-idmu','rootfs'],cwd=work,stdin=source,check=True)
        rootfs=work/'rootfs';builder.reqhash(rootfs,builder.EXPECTED['stock_rootfs'],'official rootfs')
        subprocess.run([str(un),'-d',str(work/'extracted'),str(rootfs),'opt/lib/libcolib.so'],check=True)
        library=work/'extracted/opt/lib/libcolib.so';builder.reqhash(library,LIBCO_SHA,'qualified libco')
        target.parent.mkdir(parents=True,exist_ok=True);shutil.copyfile(library,target)
    print('Local native libco recovered from verified official input; no vendor binary downloaded or published.')

if __name__=='__main__':main()
