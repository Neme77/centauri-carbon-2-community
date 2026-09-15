#!/usr/bin/env python3
"""Copy only two verified, non-secret V3.7 components into V3.8; never sign/flash."""
import hashlib
import importlib.util
from pathlib import Path
import shutil
import sys

ROOT = Path(__file__).resolve().parent
NEW = ROOT / 'cc2_builder_v3_8'
spec = importlib.util.spec_from_file_location('cc2_v38', NEW / 'cc2_firmware_builder_v3.8.py')
builder = importlib.util.module_from_spec(spec)
spec.loader.exec_module(builder)

def main():
    builder.validate_release_printer(builder.PRINTER_RELEASE_DEFAULT.read_bytes())
    pending = []
    for relative, expected in [('components/ssh/sshd', builder.EXPECTED['sshd']),
                               ('dualtrust/dual_verify.bin', builder.EXPECTED['dual_payload'])]:
        destination = NEW / relative
        if destination.exists() or destination.is_symlink():
            if not destination.is_file() or destination.is_symlink():
                raise RuntimeError('Invalid destination: ' + str(destination))
            builder.reqhash(destination, expected, relative)
            continue
        source = ROOT / 'cc2_builder_v3_7' / relative
        data = source.read_bytes()
        if hashlib.sha256(data).hexdigest() != expected:
            raise RuntimeError('V3.7 component hash mismatch: ' + relative)
        pending.append((destination, data, source.stat().st_mode & 0o777))
    for destination, data, mode in pending:
        destination.parent.mkdir(parents=True, exist_ok=True)
        with destination.open('xb') as output:
            output.write(data)
        destination.chmod(mode)
        print('Prepared:', destination)
    print('V3.8 components ready. Existing keys and V3.7 files were not changed.')
    print('Next: build_stock_v3_8.ps1 -CheckKey, then -Preflight, then build.')

if __name__ == '__main__':
    try:
        main()
    except (OSError, RuntimeError) as exc:
        sys.exit('PREPARE FAIL: ' + str(exc))
