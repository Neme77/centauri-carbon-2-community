"""Full configuration-seed regression on host and the production ARM compiler.
No real network command or native service is executed. Only the hardware profile
and observed wlan0 data are fixture inputs; resolver/state code is production.
"""
from pathlib import Path
import subprocess,tempfile,json
root=Path(__file__).resolve().parent
with tempfile.TemporaryDirectory(prefix='cc2-seed-regression-') as tmp:
 base=Path(tmp)
 for name,compiler,runner in (('host','gcc',[]),('arm','arm-linux-gnueabihf-gcc',['qemu-arm'])):
  fixture=base/name;component=fixture/'component';component.mkdir(parents=True,mode=0o700)
  tools=fixture/'tools';tools.mkdir(mode=0o700)
  (component/'profile.sh').write_text('#!/bin/sh\nexit 0\n')
  (tools/'ip').write_text('#!/bin/sh\nprintf "default via 192.0.2.1 dev wlan0\\n"\n');(tools/'ip').chmod(0o700)
  deep=fixture
  for i in range(18):deep=deep/(str(i)+'x'*35)
  deep.mkdir(parents=True);(deep/'end').write_text('fixture')
  (component/'long-path').symlink_to(deep/'end')
  assert len(str(deep/'end'))>512
  binary=fixture/'seed-test'
  flags=['-std=c11','-Os','-Wall','-Wextra','-Wpedantic','-Werror','-static','-D_FORTIFY_SOURCE=3','-DNG_TEST']
  flags += [f'-DNG_{key}="{value}"' for key,value in {'COMPONENT':component,'STATE':fixture/'state','RUN':fixture/'run','TEST_TOOLS':tools}.items()]
  subprocess.run([compiler,*flags,root/'test_seed.c','-lm','-o',binary],check=True,timeout=90)
  done=subprocess.run([*runner,binary],capture_output=True,text=True,timeout=30)
  print(name,done.stdout,done.stderr,flush=True)
  assert done.returncode==0,(name,done.returncode,done.stdout,done.stderr)
print('PASS: complete initial seed runs under fortified host and static GNU ARM/QEMU without modifying networking')
