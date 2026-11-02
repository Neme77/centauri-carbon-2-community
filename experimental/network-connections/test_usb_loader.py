"""Exercise the actual loader with fake sysfs and fake insmod/rmmod only."""
from pathlib import Path
import hashlib
import shutil
import subprocess
import tempfile

root = Path(__file__).resolve().parent
cases = ('qualified', 'blocked', 'approved', 'preloaded', 'probe-failure',
         'unload-failure', 'swap', 'foreign-binding', 'bad-hash', 'probe-loaded')
for platform, compiler, runner in (('host', 'gcc', []), ('arm', 'arm-linux-gnueabihf-gcc', ['qemu-arm'])):
    with tempfile.TemporaryDirectory(prefix='cc2-usb-loader-') as temporary:
        base = Path(temporary)
        sources = base / 'source'
        sources.mkdir()
        for source in root.glob('*.h'):
            shutil.copyfile(source, sources / source.name)
        shutil.copyfile(root / 'test_usb_loader.c', sources / 'test.c')
        pins = (sources / 'profile-pins.h').read_text()
        names = ['cc2_ue300_mii.ko', 'cc2_ue300_r8152.ko', 'cc2_ue300_abi_probe.ko', 'cc2_ue300_abi_probe_3000.ko']
        # Substitute only fixture pins, never production pins or real modules.
        import re
        for name in names:
            macro = 'NG_' + name.upper().replace('.', '_') + '_SHA'
            pins, count = re.subn(r'(#define ' + macro + r'\s*\\\s*)"[a-f0-9]{64}"',
                                 lambda m: m[1] + '"' + hashlib.sha256(name.encode()).hexdigest() + '"', pins)
            assert count == 1, macro
        (sources / 'profile-pins.h').write_text(pins)
        fixture = base / 'fixture'
        flags = ['-std=c11', '-Os', '-Wall', '-Wextra', '-Wpedantic', '-Werror', '-static', '-DNG_TEST', '-DNG_PROFILE_FIXTURE']
        flags += [f'-DNG_{key}="{fixture / value}"' for key, value in {
            'COMPONENT': 'component', 'STATE': 'state', 'RUN': 'run', 'USB_SYSFS': 'usb',
            'NET_SYSFS': 'net', 'MODULE_SYSFS': 'modules', 'TEST_TOOLS': 'tools',
        }.items()]
        binary = base / 'test'
        subprocess.run([compiler, *flags, sources / 'test.c', '-lm', '-o', binary], check=True, timeout=90)
        for case in cases:
            if fixture.exists():
                shutil.rmtree(fixture)
            for directory in ('component', 'state', 'run', 'usb/1-1', 'usb/1-1:1.0', 'net/cc2lan0',
                              'modules', 'tools', 'drivers/cc2_ue300_r8152', 'drivers/foreign'):
                (fixture / directory).mkdir(parents=True, mode=0o700, exist_ok=True)
            revision = '31fd' if case == 'qualified' else '3000'
            for name, value in {'idVendor': '0bda', 'idProduct': '8153', 'bcdDevice': revision, 'speed': '480'}.items():
                (fixture / 'usb/1-1' / name).write_text(value + '\n')
            (fixture / 'net/cc2lan0/device').symlink_to(fixture / 'usb/1-1:1.0')
            (fixture / 'usb/1-1:1.0/driver').symlink_to(fixture / ('drivers/foreign' if case == 'foreign-binding' else 'drivers/cc2_ue300_r8152'))
            component = fixture / 'component'
            for name in names:
                (component / name).write_text(name)
            (component / 'native.fixture').write_text('native fixture')
            (component / 'profile.sh').write_text('#!/bin/sh\nexit 0\n')
            (component / 'SHA256SUMS').write_text(''.join(hashlib.sha256(p.read_bytes()).hexdigest() + '  ./' + p.name + '\n' for p in sorted(component.iterdir())))
            script = f'''#!/bin/sh
set -eu
cd '{fixture}'
tool=${{0##*/}}
echo "$tool $*" >> commands
case "$tool:$1" in
  insmod:*abi_probe*.ko)
    [ '{case}' != probe-failure ] || exit 1
    name=${{1##*/}}; mkdir "modules/${{name%.ko}}";;
  rmmod:*)
    [ '{case}' != unload-failure ] || exit 1
    rmdir "modules/$1"
    if [ '{case}' = swap ];then echo 3010 > usb/1-1/bcdDevice;fi;;
  insmod:*.ko) name=${{1##*/}}; mkdir "modules/${{name%.ko}}";;
  *) exit 99;;
esac
'''
            for name in ('insmod', 'rmmod'):
                (fixture / 'tools' / name).write_text(script)
                (fixture / 'tools' / name).chmod(0o700)
            if case == 'preloaded':
                (fixture / 'modules/cc2_ue300_r8152').mkdir()
            if case == 'probe-loaded':
                (fixture / 'modules/cc2_ue300_abi_probe_3000').mkdir()
            if case == 'bad-hash':
                (component / names[0]).write_text('changed')
            succeeds = case in ('qualified', 'approved', 'preloaded')
            subprocess.run([*runner, binary, case, str(int(succeeds))], check=True, timeout=30)
            commands = (fixture / 'commands').read_text().splitlines() if (fixture / 'commands').exists() else []
            if case in ('blocked', 'bad-hash', 'probe-loaded'):
                assert not commands, (case, commands)
            elif case == 'probe-failure':
                assert len(commands) == 1 and 'abi_probe' in commands[0]
            elif case == 'preloaded':
                assert len(commands) == 2 and 'abi_probe_3000' in commands[0] and commands[1].startswith('rmmod ')
            elif case in ('swap', 'unload-failure'):
                assert len(commands) == 2, commands
            elif succeeds:
                assert len(commands) == 4 and commands[1].startswith('rmmod '), commands
                assert ('abi_probe_3000' in commands[0]) == (case != 'qualified')
        print(platform, 'actual USB loader cases passed', flush=True)
