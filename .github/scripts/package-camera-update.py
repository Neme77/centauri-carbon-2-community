from pathlib import Path
import hashlib
import json
import shutil
import subprocess
import sys

root=Path(__file__).resolve().parents[2]
sys.path.insert(0,str(root/'cc2-control/installer'))
from camera_component import PINS

runtime=root/'output/camera-runtime';runtime.mkdir(parents=True)
for name,expected in PINS.items():
    directory=Path('/tmp/qualified-recovery' if name=='libcc2-camera-recovery-r2.so' else '/tmp/qualified-reap')
    matches=list(directory.rglob(name))
    assert len(matches)==1,(name,len(matches))
    assert hashlib.sha256(matches[0].read_bytes()).hexdigest()==expected,name
    shutil.copyfile(matches[0],runtime/name)
subprocess.run([sys.executable,str(root/'cc2-control/installer/package.py'),
                '--prepared',str(root/'cc2-control/dist/cc2-control'),
                '--camera-runtime',str(runtime)],check=True)
(root/'output/camera-runtime-pins.json').write_text(json.dumps(PINS,indent=2)+'\n')
print('Pinned camera binaries packaged without rebuilding or changing them; no printer addressed.')
