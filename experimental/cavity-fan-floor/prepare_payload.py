"""Build private optional-addon files from reviewed sources; never execute vendor code."""
from pathlib import Path
import argparse,hashlib,shutil

def digest(data):return hashlib.sha256(data).hexdigest()
def qualified_network_entry(original):
    needle=b'export LD_PRELOAD="$BASE/libcc2-reactor-v2.so"\n'
    before=b"""CC2_NETWORK_STARTUP=0
if /opt/usr/cc2-network-manager/control --enabled;then
    /opt/usr/cc2-network-manager/control --ensure || true
    CC2_NETWORK_STARTUP=1
fi
"""
    after=b"""if [ "$CC2_NETWORK_STARTUP" = 1 ];then
    export CC2_NETWORK_COMPAT_TRIAL=1
    export LD_PRELOAD=/opt/usr/cc2-network-manager/libcc2-network-compat-r1.so:$LD_PRELOAD
fi
"""
    result=original.replace(needle,before+needle+after)
    assert digest(result)=='914a1f49c39e90f37c27bb50ddcf9b02459bd6895131a8051f12b6f72c940adb'
    return result

def add_floor(original,library_sha):
    needle=b'export LD_PRELOAD="$BASE/libcc2-reactor-v2.so"\n'
    assert original.count(needle)==1
    # Every external helper must finish before the qualified reactor preload
    # is exported: its constructor refuses foreign executables.
    hook="""CC2_CAVITY_PRELOAD=
FLOOR=/opt/usr/cc2-cavity-floor
if [ -f "$FLOOR/enabled" ] && [ ! -L "$FLOOR/enabled" ] &&
   [ "$(cat "$FLOOR/enabled")" = CC2-CAVITY-FLOOR-R1 ] &&
   [ -f "$FLOOR/libcc2-cavity-floor-r1.so" ] && [ ! -L "$FLOOR/libcc2-cavity-floor-r1.so" ] &&
   [ "$(sha256sum "$FLOOR/libcc2-cavity-floor-r1.so" | awk '{print $1}')" = LIBRARY_SHA ];then
    CC2_CAVITY_PRELOAD=:$FLOOR/libcc2-cavity-floor-r1.so
fi
export LD_PRELOAD="$BASE/libcc2-reactor-v2.so$CC2_CAVITY_PRELOAD"
""".replace('LIBRARY_SHA',library_sha).encode()
    return original.replace(needle,hook)

def qualified_activation(original,library_sha):
    assert digest(original)=='0883272a7734f6f2206113eb215c9fc1d612fe9ecc62605ae6b8d6e8abc4be7d'
    old=b'    case "$p" in "$REACTOR"|"$OLD:$REACTOR"|"$LIB:$REACTOR") ;;*) fail printer_preload;;esac\n'
    replacement="""    CAVITY=/opt/usr/cc2-cavity-floor/libcc2-cavity-floor-r1.so
    case "$p" in
        "$REACTOR"|"$OLD:$REACTOR"|"$LIB:$REACTOR") ;;
        "$REACTOR:$CAVITY"|"$OLD:$REACTOR:$CAVITY"|"$LIB:$REACTOR:$CAVITY")
            [ -f "$CAVITY" ] && [ ! -L "$CAVITY" ] || fail fan_trial_library
            [ "$(sha256sum "$CAVITY" | awk '{print $1}')" = LIBRARY_SHA ] || fail fan_trial_library
            grep -F "$CAVITY" "/proc/$printer/maps" >/dev/null || fail fan_trial_mapping
            ;;
        *) fail printer_preload;;
    esac
""".replace('LIBRARY_SHA',library_sha).encode()
    assert original.count(old)==1
    result=original.replace(old,replacement)
    ready=b"    if ready;then echo 'activation:already_ready';exit 0;fi\n"
    guard=b"""    # Private trial: never silently lose the optional module through the
    # legacy native-spawn path. Already-mapped network changes remain available.
    [ ! -f /opt/usr/cc2-cavity-floor/enabled ] || fail fan_trial_requires_full_idle_reboot
"""
    assert result.count(ready)==1
    return result.replace(ready,ready+guard)

def prepare(repo,module,out):
    out.mkdir(parents=True,exist_ok=False)
    original=(repo/'experimental/firmware-port/launcher.sh').read_bytes()
    old='a260865964759898cf47ed8e30e20eb8981506c1a22ada32611b9911b36b47fc'
    assert digest(original)==old
    library=module.read_bytes()
    launcher=add_floor(original,digest(library))
    entry=add_floor(qualified_network_entry(original),digest(library))
    activation=qualified_activation((repo/'experimental/network-connections/activate.sh').read_bytes(),digest(library))
    profile=(repo/'experimental/network-connections/profile.sh').read_bytes()
    assert profile.count(old.encode())==1
    (out/'launcher.sh').write_bytes(launcher)
    (out/'reactor-entry.sh').write_bytes(entry)
    (out/'activate.sh').write_bytes(activation)
    (out/'profile.sh').write_bytes(profile.replace(old.encode(),digest(launcher).encode()))
    (out/'profile.original.sha256').write_text(digest(profile)+'\n',encoding='ascii',newline='\n')
    (out/'libcc2-cavity-floor-r1.so').write_bytes(library)
    shutil.copyfile(repo/'experimental/cavity-fan-floor/manage.sh',out/'manage.sh')
    shutil.copyfile(repo/'experimental/native-display/thermal.awk',out/'thermal.awk')
    files=sorted(out.iterdir())
    (out/'SHA256SUMS').write_text(''.join(digest(file.read_bytes())+'  '+file.name+'\n' for file in files),encoding='ascii',newline='\n')
    return out

if __name__=='__main__':
    parser=argparse.ArgumentParser();parser.add_argument('--repo',type=Path,required=True);parser.add_argument('--module',type=Path,required=True);parser.add_argument('--out',type=Path,required=True)
    args=parser.parse_args();prepare(args.repo,args.module,args.out)
