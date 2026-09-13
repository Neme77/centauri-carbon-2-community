#!/usr/bin/env python3
"""Offline-only patch: exact original -> user-tested HTTP/upload combined v1."""
import argparse,importlib.util,os
from pathlib import Path
BASE=Path(__file__).resolve().parents[2]
spec=importlib.util.spec_from_file_location('cc2_builder',BASE/'cc2_firmware_builder_v3.7.py')
b=importlib.util.module_from_spec(spec);spec.loader.exec_module(b)
def main():
 p=argparse.ArgumentParser(description=__doc__);p.add_argument('input',type=Path);p.add_argument('output',type=Path);a=p.parse_args()
 if a.output.exists() or a.output.is_symlink(): raise RuntimeError('Output already exists')
 patched=b.patch_http_upload_bytes(a.input.read_bytes())
 with a.output.open('xb') as f:f.write(patched);f.flush();os.fsync(f.fileno())
 a.output.chmod(a.input.stat().st_mode&0o777)
 print('Created',a.output,'MD5',b.PRINTER_PATCHED_MD5)
if __name__=='__main__':
 try:main()
 except (RuntimeError,OSError) as e:raise SystemExit(str(e))
