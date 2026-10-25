#!/usr/bin/env python3
import subprocess
import sys
from pathlib import Path
if sys.version_info < (3, 10):
    raise SystemExit('Python 3.10 or newer is required.')
print('Canvas extended pull-back / Retrazione estesa - experimental R1')
print('1. Check / Verifica (read only)\n2. Status / Stato\n3. Install / Installa\n4. Remove / Rimuovi\nAnything else: exit / Altro: esci')
action={'1':'check','2':'status','3':'install','4':'remove'}.get(input('> ').strip())
if action:
    code=subprocess.call([sys.executable,str(Path(__file__).with_name('manage.py')),action])
    input('Press Enter / Premi Invio...')
    raise SystemExit(code)
