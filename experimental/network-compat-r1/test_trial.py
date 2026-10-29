"""Exercise the real restore function with shell command mocks, never real IPs."""
import os
from pathlib import Path
import subprocess
import tempfile
import unittest

SOURCE=Path(__file__).with_name('manage.sh').read_text()
class RestoreTests(unittest.TestCase):
    def scenario(self,ready=True,reconnect=True,services=True,locked=False):
        with tempfile.TemporaryDirectory() as directory:
            root=Path(directory);base=root/'base';state=base/'state.1.1';state.mkdir(parents=True)
            bins=root/'bin';bins.mkdir();mode=root/'mode';mode.write_text('ethernet\n')
            (root/'rule').touch();(state/'rule-intent').touch()
            if ready:(root/'ready').touch()
            if services:
                (root/'printer').touch();(root/'camera').touch()
            if locked:(state/'restoring').mkdir()
            commands={
                'ip':'''#!/bin/sh
echo "ip $*" >> "$TEST_ROOT/calls"
case "$*" in
 '-4 addr show dev wlan0') [ ! -f "$TEST_ROOT/ready" ] || echo 'inet 192.168.1.103/24 scope global wlan0';;
 '-4 route show table main') [ ! -f "$TEST_ROOT/ready" ] || echo 'default via 192.168.1.1 dev wlan0';;
 '-4 rule show') [ ! -f "$TEST_ROOT/rule" ] || echo '22040: from all lookup 200';;
 '-4 rule del priority 22040 from all table 200') rm -f "$TEST_ROOT/rule";;
esac
exit 0
''',
                'wpa_cli':'''#!/bin/sh
echo "wpa $*" >> "$TEST_ROOT/calls"
case "$*" in
 *' status') if [ -f "$TEST_ROOT/ready" ];then echo wpa_state=COMPLETED;else echo wpa_state=DISCONNECTED;fi;;
 *' reconnect') [ "$TEST_RECONNECT" = 1 ] || exit 1;touch "$TEST_ROOT/ready";;
esac
''',
                'pidof':'''#!/bin/sh
case "$1" in
 elegoo_printer) [ -f "$TEST_ROOT/printer" ] || exit 1;echo 101;;
 ai_camera) [ -f "$TEST_ROOT/camera" ] || exit 1;echo 102;;
 *) exit 1;;
esac
''',
                'sleep':'#!/bin/sh\nexit 0\n',
            }
            for name,content in commands.items():
                p=bins/name;p.write_text(content);p.chmod(0o755)
            fragment=SOURCE.split('case "${1:-check}" in',1)[0]
            fragment=fragment.replace('BASE=/tmp/cc2-network-compat-r1','BASE='+str(base))
            fragment=fragment.replace('MODE=/tmp/cc2-network-compat-r1.mode','MODE='+str(mode))
            script=root/'test.sh'
            script.write_text(fragment+f'''
PATH='{bins}':$PATH
STATE='{state}'
launch_printer(){{ echo launch_printer >> "$TEST_ROOT/calls";touch "$TEST_ROOT/printer"; }}
launch_camera(){{ echo launch_camera >> "$TEST_ROOT/calls";touch "$TEST_ROOT/camera"; }}
restore
''')
            env=dict(os.environ,TEST_ROOT=str(root),TEST_RECONNECT='1' if reconnect else '0')
            result=subprocess.run(['sh',str(script)],env=env,capture_output=True,text=True,timeout=10)
            calls=(root/'calls').read_text() if (root/'calls').exists() else ''
            return {'result':result,'calls':calls,'mode':mode.exists(),'rule':(root/'rule').exists(),'restored':(state/'restored').exists(),'error':(state/'error').exists()}
    def test_ready_wifi_removes_only_trial_selection(self):
        x=self.scenario();self.assertEqual(x['result'].returncode,0,x['result'].stderr)
        self.assertFalse(x['mode']);self.assertFalse(x['rule']);self.assertTrue(x['restored'])
        self.assertNotIn('reconnect',x['calls']);self.assertNotIn('launch_',x['calls'])
    def test_wifi_reconnect_before_route_removal(self):
        x=self.scenario(ready=False);self.assertEqual(x['result'].returncode,0,x['result'].stderr)
        self.assertLess(x['calls'].index('reconnect'),x['calls'].index('rule del'))
        self.assertFalse(x['mode']);self.assertFalse(x['rule'])
    def test_failed_wifi_recovery_keeps_wired_access(self):
        x=self.scenario(ready=False,reconnect=False);self.assertEqual(x['result'].returncode,1)
        self.assertTrue(x['mode']);self.assertTrue(x['rule']);self.assertTrue(x['error'])
        self.assertFalse(x['restored']);self.assertNotIn('rule del',x['calls'])
    def test_missing_services_get_one_native_launch_each(self):
        x=self.scenario(services=False);self.assertEqual(x['result'].returncode,0,x['result'].stderr)
        self.assertEqual(x['calls'].count('launch_printer'),1);self.assertEqual(x['calls'].count('launch_camera'),1)
    def test_concurrent_restore_does_not_mutate_network(self):
        x=self.scenario(locked=True);self.assertEqual(x['result'].returncode,1)
        self.assertEqual(x['calls'],'');self.assertTrue(x['mode']);self.assertTrue(x['rule'])
if __name__=='__main__':unittest.main()
