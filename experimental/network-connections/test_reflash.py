"""Run the actual installer/rollback against retained post-reflash files.

No vendor executable, real device, service or network command is executed.
Only fixed paths, known firmware fingerprints and control telemetry are fixtures.
"""
from pathlib import Path
import hashlib
import os
import subprocess
import tempfile

root = Path(__file__).resolve().parent
repo = root.parents[1]
sha = lambda b: hashlib.sha256(b).hexdigest()
wan = b'''#!/bin/sh
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
cases = ('retained-dhcp', 'native-dhcp', 'normal-upgrade', 'foreign-native',
         'mixed-init', 'mixed-s23', 'mixed-hotplug', 'foreign-dhcp',
         'symlink-dhcp', 'retained-original', 'damaged-component',
         'missing-manifest', 'busy', 'running', 'mapped', 'guard-refused',
         'late-guard', 'seed-failure', 'profile-failure', 'copy-failure',
         'old-move-failure', 'new-move-failure')

for shell in (['dash'], ['busybox', 'sh']):
 for case in cases:
  with tempfile.TemporaryDirectory(prefix='cc2-reflash-') as tmp:
   base = Path(tmp)
   machine, source, tools = base/'machine', base/'payload', base/'tools'
   source.mkdir(); tools.mkdir()
   def put(path, blob):
    p = machine/path.lstrip('/')
    p.parent.mkdir(parents=True, exist_ok=True); p.write_bytes(blob)
    return p
   native = b'#!/bin/sh /etc/rc.common\n# qualified DHCP fixture\n'
   captured = {
    '/etc/init.d/printer': ('e7537298955e23b7a03cab4c8d7bd241c1dfcc070883cdf51c4889cd255d1bc4', b'printer fixture'),
    '/bin/wifi_daemon': ('8e19a620921c6f4f21bde910cc3d960af5400d223493041eb96f1e921dcbe3c1', b'wifi fixture'),
    '/opt/usr/cc2-control/camera/boot-camera.sh': ('e570b84e4b5615bfea53be5c52a1befef7b78f0b91ba16e62b500652a8cdfaa5', (repo/'cc2-control/installer/camera/boot-camera.sh').read_bytes()),
    '/opt/usr/cc2-reactor-runtime-v2/launcher.sh': ('a260865964759898cf47ed8e30e20eb8981506c1a22ada32611b9911b36b47fc', (repo/'experimental/firmware-port/launcher.sh').read_bytes()),
    '/opt/usr/wifi/udhcpc_wlan0': ('b76f68e345f1221d835d1a0d6279827c5df4823c2e7fadfe0298cd49ec7bb7df', native),
    '/etc/wifi/udhcpc_wlan0': ('b76f68e345f1221d835d1a0d6279827c5df4823c2e7fadfe0298cd49ec7bb7df', native),
    '/usr/share/udhcpc/set_lan.sh': ('53cf6af55ad333d5781b0d48d89fd48bde22bc516bd22e76d45f098dc224443c', wan),
    '/etc/init.d/cc2-camera-launch': ('f05accc97f0195cf4cf481b53b643902424183bb335a27f42e6069a625813c6f', (repo/'builder/current/components/camera/firmware-launcher.sh').read_bytes()),
    '/etc/init.d/cc2-reactor-launch': ('8d4a19c9be1f6f85fe5ab9d3f116aa84400e7e6a77c455e66da636373fdaac43', (repo/'builder/current/components/reactor/firmware-launcher.sh').read_bytes()),
   }
   installer = (root/'install.sh').read_text()
   for path, (digest, blob) in captured.items():
    put(path, blob); installer = installer.replace(digest, sha(blob))
   def bind(text):
    for prefix in ('/opt/', '/etc/', '/usr/share/', '/bin/wifi_daemon', '/proc/'):
     text = text.replace(prefix, str(machine)+prefix)
    return text
   for path in ('etc/rc.d', 'etc/hotplug.d/net', 'proc'):
    (machine/path).mkdir(parents=True, exist_ok=True)
   fixed_paths = tuple(captured) + ('/etc/init.d/cc2-network-manager',
      '/etc/rc.d/S23cc2-network-manager', '/etc/hotplug.d/net/90-cc2-network-manager')
   for name in ('prepare-hooks.awk', 'native-wan-policy.awk', 'wifi-client.sh',
                'native-lease.sh', 'init.sh', 'hotplug.sh'):
    (source/name).write_bytes((root/name).read_bytes())
   script = bind(installer).replace('set -eu', 'id(){ echo 0; }\nset -eu', 1)
   script = script.replace('PATH=/usr/sbin:', 'PATH='+str(tools)+':/usr/sbin:', 1)
   (source/'install.sh').write_text(script)
   rollback = bind((root/'rollback.sh').read_text())
   rollback = rollback.replace('set -eu', 'PATH='+str(tools)+':$PATH\nexport PATH\nset -eu', 1)
   (source/'rollback.sh').write_text(rollback)
   (source/'profile.sh').write_text('#!/bin/sh\n[ "${FIXTURE_MODE:-}" != profile-failure ]\n')
   (source/'control').write_text('''#!/bin/sh
case "$1" in
 --busy) [ "${FIXTURE_MODE:-}" = busy ];;
 --guard)
  [ "${FIXTURE_MODE:-}" != guard-refused ] || exit 1
  if [ "${FIXTURE_MODE:-}" = late-guard ];then
   [ ! -e "$FIXTURE_ONCE" ] || exit 1
   : > "$FIXTURE_ONCE"
  fi;;
 --seed) [ "${FIXTURE_MODE:-}" != seed-failure ];;
 *) exit 0;;
esac
''')
   (source/'control').chmod(0o700)
   (source/'SHA256SUMS').write_text(''.join(f'{sha(p.read_bytes())}  {p.name}\n' for p in sorted(source.iterdir()) if p.is_file()))
   def run(env):
    return subprocess.run([*shell, source/'install.sh'], env=env, capture_output=True, text=True, timeout=40)
   env = dict(os.environ, FIXTURE_MODE='', FIXTURE_ONCE=str(base/'once'))
   first = run(env)
   assert first.returncode == 0, (shell, case, first.stdout, first.stderr)
   component = machine/'opt/usr/cc2-network-manager'
   state = machine/'opt/usr/cc2-network-state'
   state.mkdir(); (state/'config.bin').write_bytes(b'owner settings must remain exact')
   (state/'journal.bin').write_bytes(b'fixture idle journal')
   (machine/'etc/init.d/printer').chmod(0o775)
   if case != 'normal-upgrade':
    for path in ('/etc/wifi/udhcpc_wlan0', '/usr/share/udhcpc/set_lan.sh',
                 '/etc/init.d/cc2-camera-launch', '/etc/init.d/cc2-reactor-launch'):
     put(path, captured[path][1])
    for path in fixed_paths[-3:]:
     (machine/path.lstrip('/')).unlink()
   if case == 'native-dhcp': put('/opt/usr/wifi/udhcpc_wlan0', native)
   if case == 'foreign-native': put('/etc/init.d/cc2-camera-launch', b'foreign startup')
   if case.startswith('mixed-'):
    target = fixed_paths[{'mixed-init': -3, 'mixed-s23': -2, 'mixed-hotplug': -1}[case]]
    p = machine/target.lstrip('/')
    if case == 'mixed-s23': p.symlink_to('/missing-hook')
    else: p.write_bytes(b'mixed hook')
   if case == 'foreign-dhcp': put('/opt/usr/wifi/udhcpc_wlan0', b'foreign persistent DHCP')
   if case == 'symlink-dhcp':
    p = machine/'opt/usr/wifi/udhcpc_wlan0'; p.unlink(); p.symlink_to(machine/'etc/wifi/udhcpc_wlan0')
   if case == 'retained-original':
    (component/'native-udhcpc_wlan0').write_bytes(b'foreign retained original')
    (component/'SHA256SUMS').write_text(''.join(f'{sha(p.read_bytes())}  ./{p.relative_to(component).as_posix()}\n' for p in sorted(component.rglob('*')) if p.is_file() and p.name != 'SHA256SUMS'))
   if case == 'damaged-component': (component/'wifi-client.sh').write_bytes(b'damaged')
   if case == 'missing-manifest': (component/'startup.sha256').unlink()
   if case in ('running', 'mapped'):
    proc = machine/'proc/123'; proc.mkdir()
    if case == 'running': (proc/'exe').symlink_to(component/'control')
    (proc/'maps').write_text(str(component/'compat.so') if case == 'mapped' else '')
   def snapshot():
    result = {}
    paths = [machine/p.lstrip('/') for p in fixed_paths]
    paths += [component, state]
    for p in paths:
     entries = [p] + (sorted(p.rglob('*')) if p.is_dir() and not p.is_symlink() else [])
     for q in entries:
      key = str(q.relative_to(machine))
      if q.is_symlink(): result[key] = ('link', os.readlink(q))
      elif q.is_file(): result[key] = ('file', q.read_bytes(), q.stat().st_mode & 0o777)
      elif q.is_dir(): result[key] = ('directory', q.stat().st_mode & 0o777)
      else: result[key] = ('absent',)
    return result
   before = snapshot()
   # Fail the actual copy/rename once, leaving production rollback free to run.
   for tool in ('cp', 'mv'):
    rule = ''
    if tool == 'cp' and case == 'copy-failure':
     rule = '[ "$last" = "'+str(machine/'etc/wifi/udhcpc_wlan0')+'" ]'
    if tool == 'mv' and case == 'old-move-failure':
     rule = '[ "$1" = "'+str(component)+'" ]'
    if tool == 'mv' and case == 'new-move-failure':
     rule = '[ "$last" = "'+str(component)+'" ]'
    if rule:
     stub = tools/tool
     stub.write_text('#!/bin/sh\nfor arg;do last=$arg;done\nif [ ! -e "$FIXTURE_ONCE" ] && '+rule+';then : > "$FIXTURE_ONCE";exit 1;fi\nexec /bin/'+tool+' "$@"\n')
     stub.chmod(0o700)
   env['FIXTURE_MODE'] = case
   result = run(env)
   success = case in ('retained-dhcp', 'native-dhcp', 'normal-upgrade')
   assert (result.returncode == 0) == success, (shell, case, result.stdout, result.stderr)
   if success:
    assert (state/'config.bin').read_bytes() == b'owner settings must remain exact'
    assert (state/'journal.bin').read_bytes() == b'fixture idle journal'
    for path in ('etc/wifi/udhcpc_wlan0', 'opt/usr/wifi/udhcpc_wlan0'):
     assert (machine/path).read_bytes() == (root/'wifi-client.sh').read_bytes()
    if case != 'normal-upgrade': assert 'REFLASH RECOVERY COMPLETE' in result.stdout
    backups = sorted((machine/'opt/usr').glob('cc2-network-backup-*'))
    backup = next(p for p in backups if (p/'component').exists())
    restored = subprocess.run([*shell, backup/'restore.sh', '--installer'], env=env, capture_output=True, text=True, timeout=20)
    assert restored.returncode == 0, (case, restored.stdout, restored.stderr)
    assert snapshot() == before, (shell, case, 'manual rollback differs')
   else:
    assert snapshot() == before, (shell, case, 'refusal/automatic rollback differs', result.stdout, result.stderr)
   print('PASS:', '/'.join(shell), case, flush=True)
print('PASS: exact post-reflash recovery, preserved settings, refusal and rollback on dash/BusyBox; no printer or network accessed', flush=True)
