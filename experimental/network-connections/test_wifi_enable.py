"""Execute production reconnect against delayed native WPA socket responses."""
from pathlib import Path
import os,socket,subprocess,tempfile,threading
root=Path(__file__).resolve().parent
with tempfile.TemporaryDirectory(prefix='cc2-wifi-enable-') as tmp:
 base=Path(tmp);address=str(base/'wpa')
 flags=['-std=c11','-Os','-Wall','-Wextra','-Wpedantic','-Werror','-DNG_TEST']
 flags += [f'-DNG_{k}="{v}"' for k,v in {'STATE':base/'state','RUN':base/'run','WPA':address}.items()]
 for platform,cc,runner in (('host','gcc',[]),('arm','arm-linux-gnueabihf-gcc',['qemu-arm'])):
  binary=base/('enable-'+platform)
  subprocess.run([cc,*flags,'-static',root/'test_wifi_enable.c','-lm','-o',binary],check=True,timeout=60)
  for case in ('delayed','disabled','connected','associating','retry'):
   sock=socket.socket(socket.AF_UNIX,socket.SOCK_DGRAM);sock.bind(address);sock.settimeout(.1)
   stop=threading.Event();commands=[];errors=[];state={'reads':0,'reconnects':0,'current':''}
   def server():
    try:
     while not stop.is_set():
      try:command,peer=sock.recvfrom(4096)
      except socket.timeout:continue
      text=command.decode();commands.append(text)
      if text=='STATUS':
       state['reads']+=1
       current='DISCONNECTED'
       if case=='disabled' or (case=='delayed' and state['reads']<=3):current='INTERFACE_DISABLED'
       if case=='connected':current='COMPLETED'
       if case=='associating':current='4WAY_HANDSHAKE'
       state['current']=current;reply='wpa_state='+current+'\n'
      elif text=='RECONNECT':
       assert state['current'] not in ('INTERFACE_DISABLED','COMPLETED','4WAY_HANDSHAKE')
       state['reconnects']+=1
       reply='FAIL\n' if case=='retry' and state['reconnects']==1 else 'OK\n'
      else:raise AssertionError(text)
      sock.sendto(reply.encode(),peer)
    except BaseException as e:errors.append(repr(e))
   thread=threading.Thread(target=server);thread.start()
   try:
    result=subprocess.run([*runner,binary,'failure' if case=='disabled' else 'success'],check=True,capture_output=True,text=True,timeout=10)
    assert not errors,errors
    expected=0 if case in ('disabled','connected','associating') else 2 if case=='retry' else 1
    assert commands.count('RECONNECT')==expected,(case,commands)
    if case=='delayed':assert commands[:4]==['STATUS']*4
    assert not any('NETWORK' in x or 'SAVE_CONFIG' in x for x in commands)
    print(platform,case,'PASS',flush=True)
   finally:
    stop.set();thread.join();sock.close();os.unlink(address)
print('PASS: delayed interface readiness, bounded timeout, connected/handshake preservation and reconnect retry on host and ARM; no profiles or network changed')
