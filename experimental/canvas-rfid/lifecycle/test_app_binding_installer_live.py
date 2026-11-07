"""Actual installer template + actual configured worker + actual app bootstrap.

Only HTTP/vendor status and the service wrapper are fakes. The dummy application
is a real process and executes the production bootstrap. No serial device exists.
Publication/package validation is exercised by the separate delivery suite.
"""
from pathlib import Path
import hashlib, os, signal, subprocess, time

APP=Path(__file__).resolve().parents[3]/'cc2-control'
sha=lambda b:hashlib.sha256(b).hexdigest()


def put(path,data,mode=0o600):
    path.parent.mkdir(parents=True,exist_ok=True,mode=0o700)
    path.write_bytes(data if isinstance(data,bytes) else data.encode());path.chmod(mode)


def run(folder,component,native,app,transaction,source,old_process,new_bytes,env,shell,fail):
    stage=folder/'installer-stage';stage.mkdir(mode=0o700)
    pidfile=folder/'application.pid';put(pidfile,str(old_process.pid))
    log=folder/'application.log';put(log,b'READY\n');init=folder/'app-init'
    init_text=f'''#!/bin/sh
case "$1" in
start) "{app}" > "{log}" 2>&1 < /dev/null & echo $! > "{pidfile}";;
stop) p=$(cat "{pidfile}"); kill "$p" 2>/dev/null || true;;
esac
'''
    put(init,init_text,0o755)
    for name in ['cc2-update-worker','cc2-https-download','ca-bundle.pem','updater-trusted-key.json',
                 'THIRD_PARTY_NOTICES.txt','start.sh','launch.sh','update-runner.sh','build-info.json']:
        put(stage/name,b'inert application support data')
    put(stage/'cc2-control',new_bytes,0o755);put(stage/'cc2-control.init',init_text,0o755)
    put(stage/'web/index.html',b'inert UI');put(stage/'web/locales/en.json',b'{}')
    put(app.parent/'spools.json',b'SPOOL DATA PRESERVED')
    # A backup restore script is copied but not invoked in this test; the actual
    # packaged restore path has its own dash/BusyBox delivery qualification.
    put(stage/'restore-on-printer.sh',b'#!/bin/sh\nexit 125\n',0o755)
    commands=folder/'commands';commands.mkdir()
    status='{"connected":true,"last_message_age":1,"extruder":{"temperature":24,"target":0},"heater_bed":{"temperature":22,"target":0},"machine":{"status":1,"sub_status":0}}'
    version='failed' if fail else '1.2.26'
    health='{"service":"cc2-control","version":"'+version+'","mqtt_registered":true,"snapshot_received":true}'
    put(commands/'wget',f'''#!/bin/sh
case "$*" in
+*/api/health*) n=0; while ! grep -qx READY "{log}"; do
+    [ "$n" -lt 20 ] || exit 1; n=$((n+1)); /bin/sleep 0.1
+done; echo '{health}';;
+*) echo '{status}';;
+esac
+'''.replace('\n+','\n'),0o755)
    put(commands/'pidof',f'''#!/bin/sh
case "$1" in
elegoo_printer) echo {native.pid};;
cc2-control) p=$(cat "{pidfile}"); readlink "/proc/$p/exe" >/dev/null 2>&1 && echo "$p";;
esac
''',0o755)
    # Keep watchdog sleeps real, but accelerate the outer health retry only.
    put(commands/'sleep','#!/bin/sh\n[ "$1" != 2 ] || exit 0\nexec /bin/sleep "$@"\n',0o755)
    daemon=folder/'daemon';put(daemon,b'inert vendor daemon');memory=folder/'meminfo';put(memory,b'MemAvailable: 65000 kB\n')
    # Publication is already complete in this fixture. Use the real CLI actions
    # at the unchanged template hook positions; no worker/action is substituted.
    functions=f'''BINDING_PREPARED=0
binding_prepare() {{
    exec 3< "$LOCK"
    "{transaction}/worker" --prepare "{source}" 3 || return 1
    BINDING_PREPARED=1
}}
binding_commit() {{ "{transaction}/worker" --commit 3; }}
binding_rollback() {{ "{transaction}/worker" --rollback 3; }}
'''
    mapping={'@BINARY_SHA256@':sha(new_bytes),'@VERSION@':'1.2.26','@FIRMWARE_VERSION@':'4.4',
        '@PRINTER_SHA256@':sha(app.read_bytes()),'@DAEMON_SHA256@':sha(daemon.read_bytes()),
        '@CAMERA_COMPONENT_FUNCTIONS@':'','@CONNECTIONS_COMPONENT_FUNCTIONS@':'',
        '@RFID_COMPONENT_FUNCTIONS@':'','@APP_BINDING_FUNCTIONS@':functions,
        '@CAMERA_COMPONENT_PREFLIGHT@':'','@RFID_COMPONENT_PREFLIGHT@':'',
        '@RFID_COMPONENT_PUBLISH@':'','@APP_BINDING_PREPARE@':'binding_prepare || exit 1',
        '@APP_BINDING_COMMIT@':'binding_commit || exit 1','@CONNECTIONS_COMPONENT_INSTALL@':'',
        '@RFID_COMPONENT_COMPLETION@':'','@CAMERA_RESTORE_FUNCTION@':'','@NATIVE_UNCHANGED_ROLLBACK@':'','@CAMERA_FAILURE_RELEASE@':'rmdir "$LOCK"',
        '@APP_BINDING_ROLLBACK@':'''if [ "${BINDING_PREPARED:-0}" -eq 1 ] && ! binding_rollback; then
                echo 'Uncertain real binding rollback; retain candidate and barrier' >&2
                exit 1
            fi'''}
    script=(APP/'installer/install-on-printer.sh').read_text()
    for before,after in mapping.items():script=script.replace(before,after)
    replacements={'/opt/usr/cc2-control':str(app.parent),'/etc/init.d/cc2-control':str(init),
        '/tmp/cc2-control-install.lock':str(folder/'install.lock'),
        '/opt/bin/elegoo_printer':str(Path('/proc')/str(native.pid)/'exe'),
        '/opt/inst/daemon-000/daemon-000':str(daemon),'/proc/meminfo':str(memory)}
    for before,after in replacements.items():script=script.replace(before,after)
    script=script.replace('"$tries" -lt 120','"$tries" -lt 2')
    assert '@APP_BINDING_' not in script
    put(stage/'install-on-printer.sh',script,0o755)
    put(stage/'SHA256SUMS',''.join(sha(p.read_bytes())+'  '+p.relative_to(stage).as_posix()+'\n'
        for p in sorted(stage.rglob('*')) if p.is_file() and p.name!='SHA256SUMS'))
    original=app.read_bytes();journal=(component/'lifecycle').read_bytes()
    try:
        result=subprocess.run([*shell,stage/'install-on-printer.sh'],env=dict(env,PATH=str(commands)+':'+os.environ['PATH']),
            capture_output=True,text=True,timeout=90)
        assert (result.returncode==0)==(not fail),(shell,fail,result.stdout,result.stderr,log.read_text() if log.exists() else '')
        assert app.read_bytes()==(original if fail else new_bytes),(shell,fail,'wrong app restored')
        assert (component/'lifecycle').read_bytes()==journal,'native journal changed'
        assert (app.parent/'spools.json').read_bytes()==b'SPOOL DATA PRESERVED'
        assert not (folder/'install.lock').exists(),'completed operation retained lock'
        for name in ['coordinator','capture','app-integration.json','activate-on-ready']:
            assert (component/name).read_bytes()==(('old ' if fail else 'new ')+name).encode(),(shell,fail,name)
        current=int(pidfile.read_text());assert Path(f'/proc/{current}/exe').exists(),'verified application not running'
    finally:
        if pidfile.exists():
            pid=int(pidfile.read_text())
            # Only the process started by this fixture's own service wrapper.
            try:
                if os.readlink(f'/proc/{pid}/exe')==str(app):os.kill(pid,signal.SIGTERM)
            except ProcessLookupError:pass
            except FileNotFoundError:pass
        for _ in range(50):
            try:
                if not Path(f'/proc/{pid}/exe').exists():break
            except UnboundLocalError:break
            time.sleep(0.02)
