"""Exercise the actual disk controller with inert files and local fake APIs.

Run in the existing offline Docker builder. No native ELF is executed, no
printer is contacted, and no service or real boot tree is addressed.
"""
from pathlib import Path
import hashlib
import json
import os
import shlex
import shutil
import subprocess
import tempfile

HERE = Path(__file__).resolve().parent
sha = lambda p: hashlib.sha256(p.read_bytes()).hexdigest()


def fixture(root, scenario):
    base, gui, images = root/'component', root/'native/gui', root/'images/theme_default'
    for p in (base, gui.parent, images, root/'etc/init.d', root/'etc/rc.d', root/'proc/123', root/'bin', root/'fb'):
        p.mkdir(parents=True, exist_ok=True)
    (base/'gui').mkdir(); (base/'images').mkdir()
    gui.write_bytes(b'ORIGINAL GUI FIXTURE');gui.chmod(0o755)
    proc=root/'proc/123';(proc/'exe').write_bytes(gui.read_bytes());(proc/'maps').write_text('');(proc/'environ').write_bytes(b'PATH=/usr/bin\0')
    init=root/'etc/init.d/printer';init.write_text('UNCHANGED NATIVE STARTUP\n')
    link=root/'etc/rc.d/S95printer';link.symlink_to('../init.d/printer')
    (images.parent/'current_theme').symlink_to(str(images))
    (root/'fb/modes').write_text('U:532x300p-60\n');(root/'fb/bits').write_text('32\n')
    (base/'gui/ec-eeb001-gui').write_bytes(b'NEW GUI FIXTURE');(base/'gui/ec-eeb001-gui').chmod(0o755)
    originals={'gui/ec-eeb001-gui':gui}
    for name in ('0.png','1.png','2.png'):
        p=images/name;p.write_bytes(('ORIGINAL '+name).encode());p.chmod(0o644)
        (base/'images'/name).write_bytes(('NEW '+name).encode());(base/'images'/name).chmod(0o644)
        originals['images/'+name]=p
    (base/'pins.sh').write_text(f"GUI_SHA={sha(gui)}\nINIT_SHA={sha(init)}\n")
    (base/'file-pins.txt').write_text(''.join(f'{sha(p)} {sha(base/name)} {name}\n' for name,p in originals.items()))
    shutil.copyfile(HERE/'thermal.awk',base/'thermal.awk')
    script=(HERE/'manage.sh').read_text()
    for old,new in [('BASE=/opt/usr/cc2-native-display','BASE='+shlex.quote(str(base))),
                    ('GUI=/opt/bin/ec-eeb001-gui','GUI='+shlex.quote(str(gui))),
                    ('IMAGES=/opt/usr/images/theme_default','IMAGES='+shlex.quote(str(images))),
                    ('/opt/usr/images/current_theme',shlex.quote(str(images.parent/'current_theme'))),
                    ('/etc/rc.d/S95printer',shlex.quote(str(link))),
                    ('/etc/init.d/printer',shlex.quote(str(init))),
                    ('/sys/class/graphics/fb0/modes',shlex.quote(str(root/'fb/modes'))),
                    ('/sys/class/graphics/fb0/bits_per_pixel',shlex.quote(str(root/'fb/bits'))),
                    ('/proc/$pid/',str(proc)+'/')]:
        assert old in script,old
        script=script.replace(old,new)
    (base/'manage.sh').write_text(script);(base/'manage.sh').chmod(0o755)
    entries=[p for p in base.rglob('*') if p.is_file()]
    controls=('manage.sh','pins.sh','file-pins.txt','thermal.awk')
    (base/'CONTROL-SHA256SUMS').write_text(''.join(f'{sha(base/name)}  {name}\n' for name in controls))
    entries.append(base/'CONTROL-SHA256SUMS')
    (base/'SHA256SUMS').write_text(''.join(f'{sha(p)}  {p.relative_to(base).as_posix()}\n' for p in sorted(entries)))
    printer={'connected':True,'last_message_age':0,'machine':{'status':1,'status_name':'Idle'},
             'print':{'enabled':False,'filename':''},'extruder':{'temperature':24,'target':0},
             'heater_bed':{'temperature':24,'target':0},'bed_drying':{'active':False}}
    uds={'connected':True,'fresh':True,'print_state':'complete'}
    manager={'busy':False,'pending':None};update={'busy':False}
    if scenario=='hot':printer['extruder']['temperature']=80
    if scenario=='heating':printer['heater_bed']['target']=60
    if scenario=='printing':printer['machine']['status']=2
    if scenario=='retained-complete':printer['print']['enabled']=True
    if scenario=='active-session':printer['print']['enabled']=True;uds['print_state']='printing'
    if scenario=='network-pending':manager['pending']={'token':'fixture'}
    if scenario=='update-busy':update['busy']=True
    if scenario=='foreign-gui':gui.write_bytes(b'FOREIGN GUI')
    if scenario=='foreign-image':(images/'1.png').write_bytes(b'FOREIGN IMAGE')
    if scenario=='preload':(proc/'environ').write_bytes(b'LD_PRELOAD=/foreign.so\0')
    for name,value in [('printer',printer),('uds',uds),('manager',manager),('update',update)]:
        (root/(name+'.json')).write_text(json.dumps(value,separators=(',',':'))+'\n')
    commands=root/'bin'
    def command(name,text):
        p=commands/name;p.write_text('#!/bin/sh\n'+text+'\n');p.chmod(0o755)
    # The printer's privilege context is simulated like uname/pidof; never
    # inherit the CI account (non-root) or require sudo for temporary fixtures.
    command('id', 'case "$1" in -u) echo ' + ('1000' if scenario=='non-root' else '0') + ';; *) exit 2;; esac')
    command('uname','case "$1" in -m) echo armv7l;; -r) echo 5.4.61;; *) exit 1;; esac')
    command('pidof','echo 123')
    command('sync','exit 0')
    command('wget',f'''case "$*" in
*api/printer*) cat {shlex.quote(str(root/'printer.json'))};;
*api/uds*) cat {shlex.quote(str(root/'uds.json'))};;
*api/network/manager*) cat {shlex.quote(str(root/'manager.json'))};;
*api/app-update/status*) cat {shlex.quote(str(root/'update.json'))};;
*) exit 2;; esac''')
    if scenario=='copy-failure':
        command('cp',f'''case "$*" in *'/2.png.cc2-native-display-'*)
if [ ! -f {shlex.quote(str(root/'failed-once'))} ];then touch {shlex.quote(str(root/'failed-once'))};exit 1;fi;;esac
exec /bin/cp "$@"''')
    if scenario=='stalled-http':command('wget','exec /bin/sleep 30')
    if scenario=='symlink-image':(images/'1.png').unlink();(images/'1.png').symlink_to(root/'printer.json')
    if scenario=='bad-geometry':(root/'fb/modes').write_text('U:480x272p-60\n')
    if scenario=='damaged-payload':(base/'images/1.png').write_bytes(b'TAMPER')
    env=dict(os.environ,PATH=str(commands)+':'+os.environ['PATH'])
    before={name:p.read_bytes() for name,p in originals.items()}
    return base,gui,images,init,link,originals,before,env


def run():
    shells=[['dash'],['busybox','sh']]
    for shell in shells:
        assert shutil.which(shell[0])
        syntax=subprocess.run([*shell,'-n',str(HERE/'manage.sh')],capture_output=True,text=True)
        assert syntax.returncode==0,syntax.stderr
        for scenario in ('non-root','check','success','retained-complete','copy-failure','hot','heating','printing',
                         'active-session','network-pending','update-busy','foreign-gui','foreign-image',
                         'preload','symlink-image','bad-geometry','damaged-payload','stalled-http'):
            with tempfile.TemporaryDirectory(prefix='cc2 native display ') as tmp:
                root=Path(tmp);base,gui,images,init,link,originals,before,env=fixture(root,scenario)
                action='check' if scenario=='check' else 'install'
                result=subprocess.run([*shell,str(base/'manage.sh'),action],env=env,capture_output=True,text=True,timeout=100)
                expected=scenario in ('check','success','retained-complete')
                assert (result.returncode==0)==expected,(shell,scenario,result.stdout,result.stderr)
                assert init.read_text()=='UNCHANGED NATIVE STARTUP\n' and link.readlink()==Path('../init.d/printer')
                if scenario in ('success','retained-complete'):
                    assert all(p.read_bytes()==(base/name).read_bytes() for name,p in originals.items())
                    # Foreign edits after installation must never be overwritten.
                    (images/'1.png').write_bytes(b'FOREIGN AFTER INSTALL')
                    refused=subprocess.run([*shell,str(base/'manage.sh'),'restore'],env=env,capture_output=True,text=True,timeout=30)
                    assert refused.returncode and (images/'1.png').read_bytes()==b'FOREIGN AFTER INSTALL'
                    shutil.copyfile(base/'images/1.png',images/'1.png')
                    # Original recovery must not depend on the new artwork or
                    # candidate ELF still being intact in the staging folder.
                    (base/'images/1.png').write_bytes(b'DAMAGED UNUSED PAYLOAD')
                    restored=subprocess.run([*shell,str(base/'manage.sh'),'restore'],env=env,capture_output=True,text=True,timeout=30)
                    assert restored.returncode==0,(restored.stdout,restored.stderr)
                    assert all(p.read_bytes()==before[name] for name,p in originals.items())
                    assert list((base/'state').glob('backup-*')), 'Original backup must remain'
                else:
                    assert all(p.read_bytes()==before[name] for name,p in originals.items()),scenario
                if scenario=='non-root':assert 'STOP: root_required' in result.stderr
                if scenario=='copy-failure':assert 'ROLLBACK PASSED' in result.stderr
                assert not (base/'state/lock').exists()
                print('PASS',shell,scenario,flush=True)
    print('PASS: native disk installer/restore on dash and BusyBox; guards, rollback, exact recovery, foreign edit/symlink refusal; no printer or vendor executable accessed')


if __name__=='__main__':run()
