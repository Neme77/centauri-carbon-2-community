"""Execute the shipping disk runner using inert app/component installers."""
from pathlib import Path
import hashlib,io,subprocess,tarfile,tempfile
root=Path(__file__).resolve().parent
template=(root/'run-install.sh.in').read_text()
for shell in (['dash'],['busybox','sh']):
 for scenario in ('success','app-failed','component-failed','archive-changed','symlink-stage','embedded-success','embedded-missing','embedded-app-failed'):
  with tempfile.TemporaryDirectory(prefix='connections-runner-') as tmp:
   parent=Path(tmp);stage=parent/'stage';stage.mkdir();backup=parent/'backup';backup.mkdir()
   (backup/'keep').write_text('retained')
   archives=[]
   for kind in ('app','network'):
    target=stage/('cc2-control-payload.tar.gz' if kind=='app' else 'cc2-connections.tar.gz')
    code=7 if (kind=='app' and scenario in ('app-failed','embedded-app-failed')) or (kind=='network' and scenario=='component-failed') else 0
    script='#!/bin/sh\necho '+kind+'-installer-executed\n'
    if kind=='app': script+='echo "INSTALLATION PASSED. Backup: /opt/usr/cc2-control-backup-20000101-1"\n'
    if kind=='app' and scenario=='embedded-success':script+='echo CONNECTIONS_COMPONENT_INSTALLED=1\n'
    if kind=='network' and scenario.startswith('embedded-'):script+='echo DUPLICATE_NETWORK_INSTALL; exit 77\n'
    if kind=='network' and scenario=='archive-changed':script+=f'printf changed >> "{target}"\n'
    script+=f'exit {code}\n'
    with tarfile.open(target,'w:gz') as tar:
     data=script.encode();entry=tarfile.TarInfo('install-on-printer.sh' if kind=='app' else 'install.sh');entry.size=len(data);entry.mode=0o700;tar.addfile(entry,io.BytesIO(data))
    archives.append(hashlib.sha256(target.read_bytes()).hexdigest())
   text=template.replace('/opt/usr/cc2-control-connections-r5',str(stage)).replace('@APP_SHA@',archives[0]).replace('@NETWORK_SHA@',archives[1]).replace('@NETWORK_EMBEDDED@','1' if scenario.startswith('embedded-') else '0')
   runner=stage/'run.sh';runner.write_text(text)
   if scenario=='symlink-stage':(stage/'payload').symlink_to(backup,target_is_directory=True)
   result=subprocess.run([*shell,runner],capture_output=True,timeout=12)
   assert (backup/'keep').read_text()=='retained'
   if scenario=='symlink-stage':assert result.returncode!=0;continue
   assert (stage/'app-install.log').exists()
   success=scenario in ('success','archive-changed','embedded-success')
   assert (result.returncode==0)==success,(shell,scenario,result.stdout,result.stderr)
   for f in ('cc2-control-payload.tar.gz','cc2-connections.tar.gz','payload','connections'):
    assert (stage/f).exists()==(scenario not in ('success','embedded-success')),(shell,scenario,f)
   if scenario in ('app-failed','embedded-app-failed'):assert not (stage/'network-install.log').exists()
   else:assert (stage/'network-install.log').exists()
   if scenario.startswith('embedded-'):
    assert 'app-installer-executed' in (stage/'app-install.log').read_text()
    assert 'DUPLICATE_NETWORK_INSTALL' not in result.stdout.decode()
print('PASS: complete runner on dash/BusyBox, app/component failures, success-only cleanup, changed-archive and symlink refusal; logs/backup retained')
