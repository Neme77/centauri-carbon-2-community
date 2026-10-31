"""Execute both DHCP entry paths, actual installer ownership and rollback.
All fixed filesystem paths are bound to temporary fixtures; no live networking.
"""
from pathlib import Path
import hashlib,os,subprocess,tempfile
root=Path(__file__).resolve().parent;repo=root.parents[1]
sha=lambda b:hashlib.sha256(b).hexdigest()
wan=b'''#!/bin/sh
get_lan_net(){
 echo 192.0.2.0/24
}
set_firewall_wlan_net(){
 :
}
case "$1" in
 bound|renew) :;;
esac
'''
for shell in (['dash'],['busybox','sh']):
 for foreign in (False,True):
  with tempfile.TemporaryDirectory(prefix='cc2-wifi-entry-install-') as tmp:
   base=Path(tmp);machine=base/'machine';source=base/'payload';source.mkdir()
   def put(path,blob):
    target=machine/path.lstrip('/');target.parent.mkdir(parents=True,exist_ok=True);target.write_bytes(blob);return target
   native=b'#!/bin/sh /etc/rc.common\n# qualified DHCP fixture\n'
   captured={
    '/etc/init.d/printer':('e7537298955e23b7a03cab4c8d7bd241c1dfcc070883cdf51c4889cd255d1bc4',b'printer fixture'),
    '/bin/wifi_daemon':('8e19a620921c6f4f21bde910cc3d960af5400d223493041eb96f1e921dcbe3c1',b'wifi fixture'),
    '/opt/usr/cc2-control/camera/boot-camera.sh':('e570b84e4b5615bfea53be5c52a1befef7b78f0b91ba16e62b500652a8cdfaa5',(repo/'cc2-control/installer/camera/boot-camera.sh').read_bytes()),
    '/opt/usr/cc2-reactor-runtime-v2/launcher.sh':('a260865964759898cf47ed8e30e20eb8981506c1a22ada32611b9911b36b47fc',(repo/'experimental/firmware-port/launcher.sh').read_bytes()),
    '/opt/usr/wifi/udhcpc_wlan0':('b76f68e345f1221d835d1a0d6279827c5df4823c2e7fadfe0298cd49ec7bb7df',native),
    '/etc/wifi/udhcpc_wlan0':('b76f68e345f1221d835d1a0d6279827c5df4823c2e7fadfe0298cd49ec7bb7df',native),
    '/usr/share/udhcpc/set_lan.sh':('53cf6af55ad333d5781b0d48d89fd48bde22bc516bd22e76d45f098dc224443c',wan),
    '/etc/init.d/cc2-camera-launch':('f05accc97f0195cf4cf481b53b643902424183bb335a27f42e6069a625813c6f',(repo/'builder/current/components/camera/firmware-launcher.sh').read_bytes()),
    '/etc/init.d/cc2-reactor-launch':('8d4a19c9be1f6f85fe5ab9d3f116aa84400e7e6a77c455e66da636373fdaac43',(repo/'builder/current/components/reactor/firmware-launcher.sh').read_bytes()),
   }
   installer=(root/'install.sh').read_text()
   for path,(digest,blob) in captured.items():put(path,blob);installer=installer.replace(digest,sha(blob))
   (machine/'etc/init.d/printer').chmod(0o775)
   def bind(text):
    for prefix in ('/opt/','/etc/','/usr/share/','/bin/wifi_daemon'):text=text.replace(prefix,str(machine)+prefix)
    return text
   for path in ('etc/rc.d','etc/hotplug.d/net'):(machine/path).mkdir(parents=True,exist_ok=True)
   for name in ('prepare-hooks.awk','native-wan-policy.awk','wifi-client.sh','native-lease.sh','init.sh','hotplug.sh'):
    (source/name).write_bytes((root/name).read_bytes())
   (source/'install.sh').write_text(bind(installer).replace('set -eu','id(){ echo 0; }\nset -eu',1))
   (source/'rollback.sh').write_text(bind((root/'rollback.sh').read_text()))
   (source/'profile.sh').write_text('#!/bin/sh\nexit 0\n')
   (source/'control').write_text('#!/bin/sh\n[ "${1:-}" != --busy ]\n');(source/'control').chmod(0o700)
   (source/'SHA256SUMS').write_text(''.join(f'{sha(p.read_bytes())}  {p.name}\n' for p in sorted(source.iterdir()) if p.is_file()))
   if foreign:put('/etc/wifi/udhcpc_wlan0',b'foreign hook')
   result=subprocess.run([*shell,source/'install.sh'],capture_output=True,text=True,timeout=30)
   installed=machine/'opt/usr/cc2-network-manager'
   if foreign:
    assert result.returncode!=0 and 'native_wifi_entry_profile' in result.stdout,(result.stdout,result.stderr)
    assert not installed.exists()
    assert (machine/'etc/wifi/udhcpc_wlan0').read_bytes()==b'foreign hook'
   else:
    assert result.returncode==0,(result.stdout,result.stderr)
    assert (machine/'etc/init.d/printer').stat().st_mode & 0o777 == 0o755
    expected=(root/'wifi-client.sh').read_bytes()
    assert (machine/'etc/wifi/udhcpc_wlan0').read_bytes()==expected
    assert (machine/'opt/usr/wifi/udhcpc_wlan0').read_bytes()==expected
    manifest=(installed/'startup.sha256').read_text()
    assert '  '+str(machine/'etc/wifi/udhcpc_wlan0') in manifest
    backup=next((machine/'opt/usr').glob('cc2-network-backup-*'))
    # Emulate the installed R5e ownership: /opt delegate managed, /etc stock,
    # and the old manifest does not claim /etc. Exercise the real upgrade.
    put('/etc/wifi/udhcpc_wlan0',native)
    (machine/'etc/init.d/printer').chmod(0o775)
    (installed/'startup.sha256').write_text(''.join(line+'\n' for line in manifest.splitlines() if '  '+str(machine/'etc/wifi/udhcpc_wlan0') not in line))
    (installed/'SHA256SUMS').write_text(''.join(f'{sha(p.read_bytes())}  ./{p.relative_to(installed).as_posix()}\n' for p in sorted(installed.rglob('*')) if p.is_file() and p!=installed/'SHA256SUMS'))
    upgraded=subprocess.run([*shell,source/'install.sh'],capture_output=True,text=True,timeout=30)
    assert upgraded.returncode==0,(upgraded.stdout,upgraded.stderr)
    assert (machine/'etc/init.d/printer').stat().st_mode & 0o777 == 0o755
    assert (machine/'etc/wifi/udhcpc_wlan0').read_bytes()==expected
    upgrade_backup=next(p for p in (machine/'opt/usr').glob('cc2-network-backup-*') if p!=backup)
    undone=subprocess.run([*shell,upgrade_backup/'restore.sh','--installer'],capture_output=True,text=True,timeout=20)
    assert undone.returncode==0,(undone.stdout,undone.stderr)
    assert (machine/'etc/init.d/printer').stat().st_mode & 0o777 == 0o775
    assert (machine/'etc/wifi/udhcpc_wlan0').read_bytes()==native
    restored=subprocess.run([*shell,backup/'restore.sh','--installer'],capture_output=True,text=True,timeout=20)
    assert restored.returncode==0,(restored.stdout,restored.stderr)
    for path,(_,blob) in captured.items():assert (machine/path.lstrip('/')).read_bytes()==blob,path
    assert (machine/'etc/init.d/printer').stat().st_mode & 0o777 == 0o775
    assert not installed.exists()
print('PASS: actual installer owns both DHCP entries, records checksums, rejects foreign hook and restores both originals on dash/BusyBox')

for shell in (['dash'],['busybox','sh']):
 with tempfile.TemporaryDirectory(prefix='cc2-wifi-entry-dispatch-') as tmp:
  base=Path(tmp);component=base/'component';component.mkdir();calls=base/'calls'
  control=component/'control';control.write_text('#!/bin/sh\ncase "$1" in\n --managed) [ "$FIXTURE_MANAGED" = 1 ];;\n --client) printf "%s %s %s\\n" "$1" "$2" "$3" >> "$FIXTURE_CALLS";;\n *) exit 2;;\nesac\n');control.chmod(0o700)
  rc=base/'rc.common';rc.write_text('service=$1;shift\n. "$service"\ncase "$1" in start|boot) start_service;;stop) stop_service;;restart|reload) restart;;*) exit 2;;esac\n')
  (component/'native-udhcpc_wlan0').write_text('start_service(){ echo native-start >> "$FIXTURE_CALLS"; }\nstop_service(){ echo native-stop >> "$FIXTURE_CALLS"; }\nrestart(){ echo native-restart >> "$FIXTURE_CALLS"; }\n')
  text=(root/'wifi-client.sh').read_text().replace('/opt/usr/cc2-network-manager',str(component)).replace('/etc/rc.common',str(rc))
  for entry in ('etc/wifi/udhcpc_wlan0','opt/usr/wifi/udhcpc_wlan0'):
   target=base/entry;target.parent.mkdir(parents=True,exist_ok=True);target.write_text(text)
   for managed in ('0','1'):
    for action in ('start','boot','restart','reload','stop'):
     calls.write_text('');env=dict(os.environ,FIXTURE_MANAGED=managed,FIXTURE_CALLS=str(calls))
     run=subprocess.run([*shell,target,action],env=env,capture_output=True,text=True,timeout=5)
     assert run.returncode==0,(entry,managed,action,run.stdout,run.stderr)
     operation='start' if action in ('start','boot') else 'restart' if action in ('restart','reload') else 'stop'
     expected=f'--client {operation} wifi' if managed=='1' else 'native-'+operation
     assert calls.read_text().splitlines()==[expected],(entry,managed,action,calls.read_text())
print('PASS: both entry paths, managed Wi-Fi-only dispatch and original rc.common fallback; no DHCP or other network process executed')
