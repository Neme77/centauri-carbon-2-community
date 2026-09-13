#!/usr/bin/env python3
"""Run the existing v3.7 builder with fixed local inputs and separate logs/outputs."""
import argparse
import datetime
import hashlib
from pathlib import Path
import subprocess
import sys
import uuid

BUILDER_SHA256 = 'b2943d3aae8e33678e26080b0a1ea7d293aa9940b952af5a39c3205a586f760c'

def sha256(path):
    h = hashlib.sha256()
    with path.open('rb') as f:
        for block in iter(lambda: f.read(1024 * 1024), b''):
            h.update(block)
    return h.hexdigest()

def build_arguments(root, mode, preflight=False, check_key=False, keep_work=False):
    root = Path(root).resolve()
    if mode not in ('stock', 'community'):
        raise ValueError('Modalita non valida')
    if preflight and check_key:
        raise ValueError('Scegli preflight oppure check-key')
    builder = root / 'cc2_builder_v3_7/cc2_firmware_builder_v3.7.py'
    private = root / ('keys/cc2_stock_private.pem' if mode == 'stock' else 'keys/cc2_community_release_private.pem')
    required = [builder, private]
    command = [sys.executable, '-u', str(builder), '--signing-mode', mode, '--private-key', str(private)]
    output = None
    action = 'check_key' if check_key else ('preflight' if preflight else 'build')
    stamp = datetime.datetime.now().strftime('%Y%m%d_%H%M%S') + '_' + uuid.uuid4().hex[:8]
    stem = 'CC2_V3_7_' + mode.upper() + '_' + stamp
    if check_key:
        command.append('--check-signing-key-only')
    else:
        firmware = root / 'original_firmware/cc2_eeb001_02.01.00.00_20260707170825.zip.sig'
        aes = root / 'keys/cc2_aes_key_v1.bin'
        mk = root / 'tools/squashfs-tools-4.6.1/bin/mksquashfs'
        un = root / 'tools/squashfs-tools-4.6.1/bin/unsquashfs'
        required += [firmware, aes, mk, un]
        command += [str(firmware), '--aes-key', str(aes), '--mksquashfs', str(mk), '--unsquashfs', str(un)]
        if preflight:
            command.append('--preflight-only')
        else:
            output = root / 'output' / (stem + '.zip.sig')
            command += ['--output', str(output)]
        if keep_work:
            command.append('--keep-work')
    missing = [str(p) for p in required if not p.is_file()]
    if missing:
        raise FileNotFoundError('File mancanti:\n' + '\n'.join(missing))
    if sha256(builder) != BUILDER_SHA256:
        raise RuntimeError('Builder diverso dalla v3.7 prevista. Non avvio la costruzione.')
    return command, output, root / 'logs' / (stem + '_' + action + '.log')

def run(root, mode, preflight=False, check_key=False, keep_work=False):
    command, output, log = build_arguments(root, mode, preflight, check_key, keep_work)
    log.parent.mkdir(parents=True, exist_ok=True)
    if output is not None:
        output.parent.mkdir(parents=True, exist_ok=True)
    print('Firma:', mode, flush=True)
    print('Log:', log, flush=True)
    # stderr is merged in Python, avoiding PowerShell 5 native-stderr handling issues.
    with log.open('x', encoding='utf-8') as dst:
        dst.write('CC2 v3.7 launcher; signing mode=' + mode + '\n')
        dst.flush()
        process = subprocess.Popen(command, stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                                   text=True, encoding='utf-8', errors='replace', bufsize=1)
        try:
            for line in process.stdout:
                print(line, end='', flush=True)
                dst.write(line)
                dst.flush()
            rc = process.wait()
        except KeyboardInterrupt:
            process.terminate()
            try:
                process.wait(timeout=10)
            except subprocess.TimeoutExpired:
                process.kill()
                process.wait()
            dst.write('\nINTERRUPTED\n')
            return 130
        dst.write('\nBUILDER_EXIT_CODE=' + str(rc) + '\n')
    if rc:
        print('Operazione fallita. Leggi il log:', log, file=sys.stderr)
        return rc if rc > 0 else 1
    if output is not None:
        if not output.is_file():
            raise RuntimeError('Builder terminato senza il file atteso: controllare il log')
        digest = sha256(output)
        checksum = output.with_name(output.name + '.sha256.txt')
        with checksum.open('x', encoding='ascii') as f:
            f.write(digest + '  ' + output.name + '\n')
        print('\nFirmware:', output)
        print('SHA256:', digest)
    else:
        print('\nControllo completato. Nessun firmware costruito.')
    return 0

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--root', required=True, type=Path)
    parser.add_argument('--mode', required=True, choices=['stock', 'community'])
    group = parser.add_mutually_exclusive_group()
    group.add_argument('--preflight', action='store_true')
    group.add_argument('--check-key', action='store_true')
    parser.add_argument('--keep-work', action='store_true')
    a = parser.parse_args()
    return run(a.root, a.mode, a.preflight, a.check_key, a.keep_work)

if __name__ == '__main__':
    try:
        sys.exit(main())
    except (OSError, RuntimeError, ValueError) as exc:
        print('LAUNCH FAIL:', exc, file=sys.stderr)
        sys.exit(1)
