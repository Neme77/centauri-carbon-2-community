"""Exercise real trial-wrapper branches against isolated fake sysfs/procfs and commands.

Never executes insmod/rmmod/ip on real hardware. The kernel probe and USB driver
need separate printer qualification. These tests cover guard and cleanup policy.
"""
import os
from pathlib import Path
import subprocess
import tempfile

SOURCE = Path(__file__).with_name('manage.sh').read_text()
MII = 'cc2_ue300_mii'
DRIVER = 'cc2_ue300_r8152'
MOCK = r'''#!/usr/bin/env python3
import os,sys
from pathlib import Path
root=Path(os.environ['CC2_DRIVER_TEST_ROOT'])
scenario=os.environ['CC2_DRIVER_TEST_CASE']
command=Path(sys.argv[0]).name
module=root/'sys/module'
net=root/'sys/class/net/cc2lan0'
def event(text):
    with (root/'events').open('a') as f:f.write(text+'\n')
if command=='insmod':
    name=Path(sys.argv[1]).stem
    assert name in ['cc2_ue300_mii','cc2_ue300_r8152']
    event('load:'+name)
    if name.endswith('r8152') and scenario=='reject':sys.exit(1)
    target=module/name;target.mkdir()
    (target/'version').write_text('v1.10.11-cc2-ue300-r1' if name.endswith('r8152') else 'cc2-ue300-r1')
    if name.endswith('r8152') and scenario!='no-interface':
        (net/'device/driver').mkdir(parents=True)
        (net/'device/driver/module').symlink_to(target)
        (net/'flags').write_text('0x1003' if scenario=='auto-up' else '0x1002')
        (net/'operstate').write_text('down')
        if scenario=='route-change':
            with (root/'proc/net/route').open('a') as f:f.write('cc2lan0 00000000 0200000A 0003 0 0 0 00000000 0 0 0\n')
elif command=='rmmod':
    name=sys.argv[1]
    assert name in ['cc2_ue300_mii','cc2_ue300_r8152']
    event('unload:'+name)
    if name.endswith('mii') and (module/'cc2_ue300_r8152').exists():sys.exit(1)
    target=module/name
    (target/'version').unlink();target.rmdir()
    if name.endswith('r8152') and net.exists():
        (net/'device/driver/module').unlink()
        (net/'device/driver').rmdir();(net/'device').rmdir()
        (net/'flags').unlink();(net/'operstate').unlink();net.rmdir()
elif command=='sha256sum':
    sys.exit(1 if scenario=='network-mismatch' else 0)
elif command=='ip':
    if '-4' in sys.argv and 'cc2lan0' in sys.argv and scenario=='auto-ip':print('    inet 10.0.0.9/24 scope global cc2lan0')
    else:print('mock interface without assigned IPv4')
elif command in ['sleep','dmesg']:
    pass
else:
    raise AssertionError(command)
'''

def fixture(root, scenario):
    for folder in ['sys/module', 'sys/class/net', 'etc/config', 'etc/hotplug.d/net', 'etc/hotplug.d/iface', 'etc/sysctl.d', 'proc/net', 'bin', 'payload']:
        (root/folder).mkdir(parents=True, exist_ok=True)
    (root/'etc/config/network').write_text("config interface 'lan'\n option ifname 'eth0'\n")
    (root/'etc/hotplug.d/net/00-sysctl').write_text('reviewed fixture')
    (root/'etc/hotplug.d/iface/00-netstate').write_text('reviewed fixture')
    (root/'etc/sysctl.conf').write_text('')
    (root/'proc/modules').write_text('')
    (root/'proc/meminfo').write_text('MemAvailable: 30000 kB\n')
    (root/'proc/net/route').write_text('Iface Destination Gateway Flags RefCnt Use Metric Mask MTU Window IRTT\nwlan0 00000000 0100000A 0003 0 0 0 00000000 0 0 0\n')
    text=SOURCE
    for prefix in ['/sys/', '/proc/', '/etc/']:
        text=text.replace(prefix,str(root)+prefix)
    (root/'payload/manage.sh').write_text(text)
    (root/'payload/abi-probe.sh').write_text('exit 0\n')
    (root/'payload/network.sha256').write_text('fixture\n')
    for command in ['insmod','rmmod','sha256sum','ip','sleep','dmesg']:
        path=root/'bin'/command;path.write_text(MOCK);path.chmod(0o755)
    return dict(os.environ, CC2_DRIVER_TEST_ROOT=str(root), CC2_DRIVER_TEST_CASE=scenario, PATH=str(root/'bin')+os.pathsep+os.environ['PATH'])

with tempfile.TemporaryDirectory() as temporary:
    for scenario in ['success','reject','no-interface','auto-up','auto-ip','route-change','network-mismatch']:
        root=Path(temporary)/scenario
        env=fixture(root,scenario)
        result=subprocess.run(['sh',str(root/'payload/manage.sh'),'start'],env=env,capture_output=True,text=True,timeout=10)
        assert (result.returncode==0)==(scenario=='success'), (scenario,result.stdout,result.stderr)
        if scenario=='success':
            assert (root/'sys/module'/DRIVER).exists()
            stopped=subprocess.run(['sh',str(root/'payload/manage.sh'),'stop'],env=env,capture_output=True,text=True,timeout=10)
            assert stopped.returncode==0,(stopped.stdout,stopped.stderr)
        assert not (root/'sys/module'/DRIVER).exists(),scenario
        assert not (root/'sys/module'/MII).exists(),scenario
        events=(root/'events').read_text().splitlines() if (root/'events').exists() else []
        if scenario=='network-mismatch':assert not events
        if scenario=='success':assert events==['load:'+MII,'load:'+DRIVER,'unload:'+DRIVER,'unload:'+MII]
        print('PASS:',scenario)
    root=Path(temporary)/'foreign';env=fixture(root,'success')
    module=root/'sys/module'/DRIVER;module.mkdir();(module/'version').write_text('different-version')
    result=subprocess.run(['sh',str(root/'payload/manage.sh'),'stop'],env=env,capture_output=True,text=True)
    assert result.returncode!=0 and module.exists() and not (root/'events').exists()
    print('PASS: foreign module version is never unloaded')
print('PASS: wrapper guards, dependency order and failure cleanup; no real module loaded')
