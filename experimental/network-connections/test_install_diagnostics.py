"""Execute the component installer up to a real transform refusal in a sandbox.
Only fixed /opt,/etc,/usr/share paths and native fingerprints are fixture-bound;
the actual preparation, failure handler and phase logging are executed unchanged.
"""
from pathlib import Path
import hashlib,subprocess,tempfile
root=Path(__file__).resolve().parent
repo=root.parents[1]
sha=lambda b:hashlib.sha256(b).hexdigest()
for shell in (['dash'],['busybox','sh']):
 with tempfile.TemporaryDirectory(prefix='connections-installer-diagnostic-') as tmp:
  base=Path(tmp);source=base/'source';source.mkdir();machine=base/'machine';machine.mkdir()
  def put(path,data):
   p=machine/path.lstrip('/');p.parent.mkdir(parents=True,exist_ok=True);p.write_bytes(data);return p
  captured={
   '/etc/init.d/printer':('e7537298955e23b7a03cab4c8d7bd241c1dfcc070883cdf51c4889cd255d1bc4',b'printer fixture'),
   '/bin/wifi_daemon':('8e19a620921c6f4f21bde910cc3d960af5400d223493041eb96f1e921dcbe3c1',b'wifi fixture'),
   '/opt/usr/cc2-control/camera/boot-camera.sh':('e570b84e4b5615bfea53be5c52a1befef7b78f0b91ba16e62b500652a8cdfaa5',(repo/'cc2-control/installer/camera/boot-camera.sh').read_bytes()),
   '/opt/usr/cc2-reactor-runtime-v2/launcher.sh':('a260865964759898cf47ed8e30e20eb8981506c1a22ada32611b9911b36b47fc',(repo/'experimental/firmware-port/launcher.sh').read_bytes()),
   '/opt/usr/wifi/udhcpc_wlan0':('b76f68e345f1221d835d1a0d6279827c5df4823c2e7fadfe0298cd49ec7bb7df',b'native client fixture'),
   '/usr/share/udhcpc/set_lan.sh':('53cf6af55ad333d5781b0d48d89fd48bde22bc516bd22e76d45f098dc224443c',b'#!/bin/sh\n# Unsupported layout: must refuse before changing startup\n'),
   '/etc/init.d/cc2-camera-launch':('f05accc97f0195cf4cf481b53b643902424183bb335a27f42e6069a625813c6f',(repo/'builder/current/components/camera/firmware-launcher.sh').read_bytes()),
   '/etc/init.d/cc2-reactor-launch':('8d4a19c9be1f6f85fe5ab9d3f116aa84400e7e6a77c455e66da636373fdaac43',(repo/'builder/current/components/reactor/firmware-launcher.sh').read_bytes()),
  }
  script=(root/'install.sh').read_text()
  for path,(old_hash,data) in captured.items():put(path,data);script=script.replace(old_hash,sha(data))
  for prefix in ('/opt/','/etc/','/usr/share/','/bin/wifi_daemon'):script=script.replace(prefix,str(machine)+prefix)
  script=script.replace('set -eu','id(){ echo 0; }\nset -eu',1)
  for name in ('prepare-hooks.awk','native-wan-policy.awk','rollback.sh'):(source/name).write_bytes((root/name).read_bytes())
  (source/'install.sh').write_text(script)
  (source/'control').write_text('#!/bin/sh\nexit 0\n');(source/'control').chmod(0o700)
  (source/'SHA256SUMS').write_text(''.join(f'{sha(p.read_bytes())}  {p.name}\n' for p in sorted(source.iterdir()) if p.is_file()))
  result=subprocess.run([*shell,source/'install.sh'],capture_output=True,timeout=15)
  assert result.returncode==1,(shell,result.stdout,result.stderr)
  assert b'STOP: wan_policy_transform' in result.stdout
  assert b'CONNECTIONS_INSTALL_EXIT=1 LAST_PHASE=prepare-wan-policy' in result.stdout
  assert b'matched=0 expected=3' in result.stderr
  assert not (machine/'opt/usr/cc2-network-manager').exists()
  for path,(_,data) in captured.items():assert (machine/path.lstrip('/')).read_bytes()==data
print('PASS: actual component installer reports failed WAN phase and preserves all native files on dash and BusyBox')
