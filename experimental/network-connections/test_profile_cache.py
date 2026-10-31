"""Exercise the production same-boot qualification cache on host and ARM."""
from pathlib import Path
import hashlib,subprocess,tempfile
root=Path(__file__).resolve().parent
for platform,cc,runner in (('host','gcc',[]),('arm','arm-linux-gnueabihf-gcc',['qemu-arm'])):
 with tempfile.TemporaryDirectory(prefix='cc2-profile-cache-') as tmp:
  base=Path(tmp);component=base/'component';component.mkdir(mode=0o700)
  (component/'payload.bin').write_bytes(b'valid')
  (component/'native.fixture').write_bytes(b'native')
  (component/'profile.sh').write_bytes(b'profile fixture')
  # Exercise a real symlink in startup.sha256, as used by /etc/rc.d.
  (component/'entry').symlink_to(component/'profile.sh')
  (component/'startup.sha256').write_text(hashlib.sha256(b'profile fixture').hexdigest()+'  '+str(component/'entry')+'\n')
  (component/'SHA256SUMS').write_text(''.join(hashlib.sha256((component/name).read_bytes()).hexdigest()+'  ./'+name+'\n' for name in ('payload.bin','profile.sh','startup.sha256')))
  binary=base/'cache-test'
  flags=['-std=c11','-Os','-Wall','-Wextra','-Wpedantic','-Werror','-static','-D_FORTIFY_SOURCE=3','-DNG_TEST','-DNG_PROFILE_FIXTURE']
  flags += [f'-DNG_{k}="{v}"' for k,v in {'COMPONENT':component,'STATE':base/'state','RUN':base/'run'}.items()]
  subprocess.run([cc,*flags,root/'test_profile_cache.c','-lm','-o',binary],check=True,timeout=90)
  done=subprocess.run([*runner,binary],check=True,capture_output=True,text=True,timeout=30)
  print(platform,done.stdout,flush=True)
print('PASS: host and GNU ARM/QEMU profile-cache regression; no native service or real network invoked')
