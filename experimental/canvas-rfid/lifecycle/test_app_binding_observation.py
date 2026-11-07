"""The update acquires data, without granting permission from an expired cache."""
from pathlib import Path
import os
import subprocess
import tempfile

here=Path(__file__).resolve().parent
with tempfile.TemporaryDirectory(prefix='cc2-observation-acquire-') as temporary:
    for compiler,prefix,extra in [('gcc',[],[]),('arm-linux-gnueabihf-gcc',['qemu-arm'],['-static']),
                                  ('gcc',[],['-fsanitize=address,undefined','-fno-sanitize-recover=all'])]:
        binary=Path(temporary)/('test-'+str(len(extra))+'-'+compiler)
        subprocess.run([compiler,'-O2','-std=c11','-D_POSIX_C_SOURCE=200809L','-Wall','-Wextra','-Wpedantic','-Werror',
                        *extra,str(here/'test_app_binding_observation.c'),'-lm','-o',str(binary)],check=True,timeout=120)
        subprocess.run([*prefix,str(binary)],check=True,timeout=15)
