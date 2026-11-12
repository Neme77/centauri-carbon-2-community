"""Both native entry routes, using fake policy/recovery/entry programs only."""
from pathlib import Path
import hashlib
import os
import subprocess
import tempfile

HERE = Path(__file__).resolve().parent

def run():
    cases = ['stock', 'guarded', 'recover', 'recover-failed', 'bad-policy',
             'bad-post-policy', 'missing-worker', 'changed-worker', 'missing-guard-entry']
    count = 0
    with tempfile.TemporaryDirectory(prefix='cc2-boot-dispatch-') as temporary:
        root = Path(temporary)
        for shell in [['dash'], ['busybox', 'sh']]:
            for route in ['reactor', 'network']:
                for case in cases:
                    folder = root / f'{shell[0]}-{route}-{case}'
                    folder.mkdir(mode=0o700)
                    worker, stock, guard = [folder / n for n in ['worker', 'stock-entry', 'guard-entry']]
                    trace = folder / 'trace'
                    worker.write_text('''#!/bin/sh
set -eu
printf '%s\\n' "$1" >> "$CASE_ROOT/trace"
case "$1" in
 --boot-policy)
  if [ -f "$CASE_ROOT/restored" ]; then
   [ "$CASE_NAME" != bad-post-policy ] || exit 0
   exit 10
  fi
  case "$CASE_NAME" in
   stock) exit 0;;
   guarded|missing-guard-entry) exit 10;;
   recover|recover-failed|bad-post-policy) exit 20;;
   *) exit 1;;
  esac;;
 --recover-original)
  [ "$CASE_NAME" != recover-failed ] || exit 1
  printf done > "$CASE_ROOT/restored";;
 *) exit 2;;
esac
''')
                    stock.write_text('#!/bin/sh\nprintf "STOCK:%s\\n" "$*" >> "$CASE_ROOT/trace"\n')
                    guard.write_text('#!/bin/sh\nprintf "GUARDED:%s\\n" "$*" >> "$CASE_ROOT/trace"\n')
                    for p in [worker, stock, guard]: p.chmod(0o700)
                    text = (HERE / 'boot-dispatch.sh').read_text()
                    replacements = {
                        'WORKER=/opt/usr/cc2-rfid/recover-worker': f'WORKER={worker}',
                        'WORKER_SHA=UNCONFIGURED': f'WORKER_SHA={hashlib.sha256(worker.read_bytes()).hexdigest()}',
                        'STOCK_ENTRY=/opt/usr/cc2-rfid/stock-entry.sh': f'STOCK_ENTRY={stock}',
                        'GUARDED_ENTRY=/opt/usr/cc2-rfid/guarded-entry.sh': f'GUARDED_ENTRY={guard}',
                    }
                    for old, new in replacements.items():
                        assert text.count(old) == 1
                        text = text.replace(old, new)
                    dispatch = folder / 'dispatch'
                    dispatch.write_text(text)
                    if case == 'missing-worker': worker.unlink()
                    if case == 'changed-worker': worker.write_text('#!/bin/sh\nexit 0\n')
                    if case == 'missing-guard-entry': guard.unlink()
                    # Both qualified native routes must enter the dispatcher;
                    # the test deliberately provides no alternative fallback.
                    entry = folder / (route + '-entry')
                    entry.write_text(f'#!/bin/sh\nexec {shell[0]} ' + ('sh ' if shell[0] == 'busybox' else '') + f'{dispatch} "$@"\n')
                    result = subprocess.run([*shell, entry, 'printer.cfg'], env=dict(os.environ, CASE_ROOT=str(folder), CASE_NAME=case),
                                            text=True, capture_output=True, timeout=10)
                    lines = trace.read_text().splitlines() if trace.exists() else []
                    okay = case in ['stock', 'guarded', 'recover']
                    assert (result.returncode == 0) == okay, (shell, route, case, result.returncode, result.stdout, result.stderr, lines)
                    launches = [s for s in lines if s.startswith(('STOCK:', 'GUARDED:'))]
                    assert launches == ([('STOCK:' if case == 'stock' else 'GUARDED:') + 'printer.cfg'] if okay else []), (case, lines)
                    assert lines.count('--recover-original') <= 1
                    if case in ['missing-worker', 'changed-worker']: assert not lines
                    if case == 'recover': assert lines == ['--boot-policy', '--recover-original', '--boot-policy', 'GUARDED:printer.cfg']
                    count += 1
    print(f'PASS: {count} dash/BusyBox dispatch cases across reactor/network entry fixtures; no vendor fallback after recovery refusal.', flush=True)
    return count

if __name__ == '__main__': run()
