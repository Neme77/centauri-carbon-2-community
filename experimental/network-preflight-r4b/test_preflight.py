"""Run the complete preflight against idle/completed/active native states."""
from pathlib import Path
import json, os, shutil, subprocess, sys, tempfile

root=Path(__file__).resolve().parent
original=(root/'preflight.before.sh').read_text()
corrected=(root/'preflight.sh').read_text()
thermal=(root/'thermal.awk').read_text()
default={'connected':True,'messages':180,'last_message_age':0,
 'extruder':{'temperature':29.1,'target':0.0},'heater_bed':{'temperature':24.7,'target':0.0},
 'machine':{'status':1,'status_name':'Idle','progress':100},
 'print':{'enabled':True,'filename':'','state':'','native':{'state':'complete','filename':'local/test.gcode'}}}
uds_default={'connected':True,'fresh':True,'messages':100,'print_state':'complete'}
cases=[('complete',{}, {},True),('idle',{'print':{'enabled':False,'native':{'state':'standby'}}},{},True),
 ('printing',{'print':{'enabled':True,'native':{'state':'printing'}}},{'print_state':'printing'},False),
 ('paused',{'print':{'enabled':True,'native':{'state':'paused'}}},{'print_state':'paused'},False),
 ('standby-enabled',{'print':{'enabled':True,'native':{'state':'standby'}}},{'print_state':'standby'},False),
 ('unknown-native',{'print':{'enabled':True,'native':None}},{},False),
 ('unknown-enabled',{'print':{'native':{'state':'complete'}}},{},False),
 ('uds-stale',{}, {'fresh':False},False),('uds-disconnected',{}, {'connected':False},False),
 ('uds-printing',{}, {'print_state':'printing'},False),('uds-paused',{}, {'print_state':'paused'},False),
 ('uds-unknown',{}, {'print_state':''},False),('uds-failed',{}, {},False),
 ('mqtt-disconnected',{'connected':False},{},False),
 ('machine-busy',{'machine':{'status':2}},{},False),
 ('hot',{'extruder':{'temperature':70,'target':0}},{},False),
 ('heater-target',{'heater_bed':{'temperature':24,'target':50}},{},False),
 ('background',{'bed_drying':{'active':True}},{},False),
 ('escaped-name',{'print':{'enabled':True,'filename':'"native":{"state":"complete",','native':{'state':'printing'}}},{},False),
 ('uds-timeout',{}, {},False)]
with tempfile.TemporaryDirectory(prefix='cc2-preflight-') as tmp:
 base=Path(tmp);bin=base/'bin';bin.mkdir();sysfs=base/'sys';(sysfs/'class/net/cc2lan0').mkdir(parents=True)
 (sysfs/'class/net/wlan0').mkdir(parents=True)
 (sysfs/'class/net/cc2lan0/carrier').write_text('1\n');(sysfs/'class/net/wlan0/flags').write_text('0x1003\n')
 (base/'thermal.awk').write_text(thermal)
 fixtures=base/'fixtures';fixtures.mkdir()
 def command(name,text):
  p=bin/name;p.write_text(text);p.chmod(0o700)
 command('id','#!/bin/sh\necho 0\n')
 command('wpa_cli','#!/bin/sh\necho wpa_state=COMPLETED\n')
 command('ip','''#!/bin/sh
case "$*" in
 '-4 addr show dev cc2lan0') echo 'inet 192.168.1.107/32 scope global cc2lan0';;
 '-4 addr show dev wlan0') echo 'inet 192.168.1.103/24 scope global wlan0';;
 '-4 rule show') echo '22030: from 192.168.1.107 lookup 200';;
 '-4 route show table 200') echo 'default via 192.168.1.1 dev cc2lan0';;
 '-4 route show table main') echo 'default via 192.168.1.1 dev wlan0';;
 *) echo 'Unexpected network command' >&2;exit 91;;
esac
''')
 command('wget','''#!/bin/sh
case "$2" in
 */api/printer) cat "$FIXTURES/printer.json";;
 */api/uds)
   if [ "$CASE" = uds-failed ];then exit 3;fi
   if [ "$CASE" = uds-timeout ];then exec sleep 20;fi
   cat "$FIXTURES/uds.json";;
 */api/app-update/status) echo '{"busy":false}';;
 *) exit 92;;
esac
''')
 env=dict(os.environ,CC2_NETWORK_PANEL_LOCAL_IP='192.168.1.107',FIXTURES=str(fixtures))
 def run(text,case):
  transformed=text.replace('PATH=/usr/sbin:/usr/bin:/sbin:/bin','PATH='+str(bin)+':/usr/sbin:/usr/bin:/sbin:/bin').replace('/sys/class/net/',str(sysfs)+'/class/net/')
  # BusyBox standalone ash may prefer its own applets over PATH fixtures.
  # Shell functions bind the mock commands explicitly without altering the guard.
  functions=''.join(name+'() { \"'+str(bin/name)+'\" \"$@\"; }\n' for name in ('id','ip','wpa_cli','wget'))
  transformed=transformed.replace('export PATH\n','export PATH\n'+functions)
  script=base/'preflight.sh';script.write_text(transformed)
  return subprocess.run(os.environ.get('TEST_SHELL','sh').split()+[str(script)],env=dict(env,CASE=case),capture_output=True,text=True,timeout=12)
 (fixtures/'printer.json').write_text(json.dumps(default,separators=(',',':')))
 (fixtures/'uds.json').write_text(json.dumps(uds_default,separators=(',',':')))
 regression=run(original,'complete');assert regression.returncode!=0 and 'Print session still enabled' in regression.stderr,(regression.stdout,regression.stderr)
 for name,printer,uds,accept in cases:
  (fixtures/'printer.json').write_text(json.dumps(dict(default,**printer),separators=(',',':')))
  (fixtures/'uds.json').write_text(json.dumps(dict(uds_default,**uds),separators=(',',':')))
  result=run(corrected,name)
  assert (result.returncode==0)==accept,(name,result.stdout,result.stderr)
  assert not list(base.glob('status-read.*')),name
print('PASS: original completed-print defect reproduced; '+str(len(cases))+' full preflight cases; fresh UDS/cold/idle guards and bounded-read cleanup')

