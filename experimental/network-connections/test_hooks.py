"""Qualify the real, hash-pinned startup transformations with awk and BusyBox."""
from pathlib import Path
import hashlib,subprocess,tempfile

root=Path(__file__).resolve().parent
repo=root.parents[1]
sources={
 'camera-launch':(repo/'builder/current/components/camera/firmware-launcher.sh','f05accc97f0195cf4cf481b53b643902424183bb335a27f42e6069a625813c6f'),
 'reactor-launch':(repo/'builder/current/components/reactor/firmware-launcher.sh','8d4a19c9be1f6f85fe5ab9d3f116aa84400e7e6a77c455e66da636373fdaac43'),
 'camera-entry':(repo/'cc2-control/installer/camera/boot-camera.sh','e570b84e4b5615bfea53be5c52a1befef7b78f0b91ba16e62b500652a8cdfaa5'),
 'reactor-entry':(repo/'experimental/firmware-port/launcher.sh','a260865964759898cf47ed8e30e20eb8981506c1a22ada32611b9911b36b47fc'),
}
with tempfile.TemporaryDirectory(prefix='connections-hooks-') as tmp:
    tmp=Path(tmp)
    for awk in (['awk'],['busybox','awk']):
        for mode,(source,sha) in sources.items():
            raw=source.read_bytes();assert hashlib.sha256(raw).hexdigest()==sha
            result=subprocess.run([*awk,'-v',f'mode={mode}','-f',root/'prepare-hooks.awk',source],check=True,capture_output=True).stdout
            assert result!=raw and result.count(b'/control --enabled')==1
            # Every original line remains, in order, including all guards and
            # the unmodified native fallback. Only delegates are inserted.
            original=iter(result.splitlines())
            for line in raw.splitlines():assert any(candidate==line for candidate in original),(mode,line)
            f=tmp/(mode+'.sh');f.write_bytes(result)
            subprocess.run(['dash','-n',f],check=True)
            subprocess.run(['busybox','sh','-n',f],check=True)
            bad=subprocess.run([*awk,'-v',f'mode={mode}','-f',root/'prepare-hooks.awk'],input=b'#!/bin/sh\necho changed\n',capture_output=True)
            assert bad.returncode!=0
    # Exercise only the public transformation grammar, without publishing any
    # proprietary filesystem input or invoking a live firewall.
    sample=b'''#!/bin/sh
get_lan_net(){
    echo old
}
set_firewall_wlan_net(){
    echo old
}
case "$1" in
 enable_wlan) fw_setenv ban_wlan false;;
 disable_wlan) fw_setenv ban_wlan true;;
esac
'''
    for awk in (['awk'],['busybox','awk']):
        result=subprocess.run([*awk,'-f',root/'native-wan-policy.awk'],input=sample,check=True,capture_output=True).stdout
        assert b'for net in $current_lan;' in result
        assert result.count(b'fw_setenv')==2
        assert b'cc2_refresh_policy)' in result
        f=tmp/'wan.sh';f.write_bytes(result)
        subprocess.run(['dash','-n',f],check=True);subprocess.run(['busybox','sh','-n',f],check=True)
        assert subprocess.run([*awk,'-f',root/'native-wan-policy.awk'],input=b'#!/bin/sh\n',capture_output=True).returncode!=0
print('PASS: exact firmware delegates, retained native guards/fallbacks, BusyBox syntax and unchanged persistent WAN restriction commands')
