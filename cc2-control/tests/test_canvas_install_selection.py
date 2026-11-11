"""Optional Canvas selection uses the candidate's native console probe."""
from pathlib import Path
import subprocess
import sys
import tempfile

ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'installer'))
from native_delivery import CANVAS_SELECTION

with tempfile.TemporaryDirectory(prefix='cc2-canvas-install-') as temp:
    stage=Path(temp)
    for shell in [['dash'],['busybox','sh']]:
        for status in (0,2,3):
            app=stage/'cc2-control'
            app.write_text(f'#!/bin/sh\n[ "$*" = "--installer-native-canvas" ] || exit 99\nexit {status}\n')
            app.chmod(0o700)
            script='set -eu\nSTAGE=$1\n'+CANVAS_SELECTION+'\nnative_canvas_available\n'
            result=subprocess.run([*shell,'-c',script,'fixture',str(stage)],timeout=5)
            assert result.returncode==status,(shell,status,result.returncode)
print('PASS: direct Canvas probe preserves connected, absent and unknown outcomes on dash and BusyBox')
