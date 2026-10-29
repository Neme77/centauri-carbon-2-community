"""Exercise the actual CC2 HTTP handler while a native WPA read is slow."""
import concurrent.futures
import json
from pathlib import Path
import socket
import subprocess
import sys
import tempfile
import threading
import time
import urllib.request
import urllib.error

binary=Path(sys.argv[1]).resolve()
peer=Path('/tmp/cc2-network-http-test.sock')
assert not peer.exists(), 'refuse to replace another fixture socket'
server=socket.socket(socket.AF_UNIX,socket.SOCK_DGRAM)
server.bind(str(peer));server.settimeout(.1)
done=threading.Event();requests=[];errors=[];mode={'reply':False}

def serve():
    while not done.is_set():
        try: command,address=server.recvfrom(256)
        except socket.timeout: continue
        except OSError: return
        requests.append(command)
        if command not in (b'STATUS',b'SIGNAL_POLL'):
            errors.append(command);return
        if not mode['reply']:continue
        answer=b'wpa_state=COMPLETED\nssid=HTTP fixture\nfreq=5220\n' if command==b'STATUS' else b'RSSI=-47\n'
        try:server.sendto(answer,address)
        except OSError as e:errors.append(str(e))

worker=threading.Thread(target=serve,daemon=True);worker.start()
probe=socket.socket();probe.bind(('127.0.0.1',0));port=probe.getsockname()[1];probe.close()
base=f'http://127.0.0.1:{port}'

def call(path,method='GET',headers=None):
    req=urllib.request.Request(base+path,method=method,headers=headers or {})
    try:
        with urllib.request.urlopen(req,timeout=3) as response:return response.status,json.load(response)
    except urllib.error.HTTPError as e:return e.code,e.read()

try:
    with tempfile.TemporaryDirectory(prefix='cc2-http-network-') as tmp:
        state=Path(tmp)
        (state/'cc2-control.conf').write_text('mqtt_username=elegoo\nmqtt_password=fixture\n')
        process=subprocess.Popen([binary,'--port',str(port),'--panda-port','0',
                                  '--config',str(state/'cc2-control.conf')],cwd=state,
                                 stdout=subprocess.DEVNULL,stderr=subprocess.DEVNULL)
        try:
            for _ in range(100):
                try:assert call('/api/health')[0]==200;break
                except OSError:
                    assert process.poll() is None;time.sleep(.05)
            else:raise AssertionError('backend startup failed')
            before=time.monotonic();status,initial=call('/api/network')
            assert status==200 and initial['refreshing'] is True and initial['available'] is False,initial
            assert time.monotonic()-before<.25, 'network read blocked its HTTP response'
            before=time.monotonic()
            assert call('/api/health')[0]==200
            assert call('/api/network/trial')[1]['available'] is False
            assert call('/api/network/select','POST',{'X-CC2-Request':'1'})[0]==400
            assert time.monotonic()-before<.25, 'slow WPA read blocked unrelated HTTP'
            for _ in range(50):
                status,data=call('/api/network')
                if data['available'] and not data['refreshing']:break
                time.sleep(.03)
            else:raise AssertionError('network sample did not finish')
            assert data['fresh'] and data['snapshot']['wireless']['status_read']['error']=='timeout'
            assert requests==[b'STATUS',b'STATUS'],requests
            count=len(requests)
            time.sleep(3.2)
            assert len(requests)==count,'unopened network tab caused background polling'
            mode['reply']=True
            with concurrent.futures.ThreadPoolExecutor(max_workers=4) as pool:
                result=list(pool.map(lambda _:call('/api/network'),range(16)))
            assert all(code==200 for code,_ in result),result
            for _ in range(30):
                _,data=call('/api/network')
                if not data['refreshing'] and data['snapshot']['wireless']['control_available']:break
                time.sleep(.03)
            else:raise AssertionError('valid native data not returned')
            assert data['snapshot']['wireless']['signal_dbm']==-47
            assert data['snapshot']['wireless']['ssid']=='HTTP fixture'
            assert len(requests)==count+2,requests
            for method in ('POST','PUT','DELETE'):
                assert call('/api/network',method,{'X-CC2-Request':'1'})[0]==405
            assert call('/api/network/select','POST',{'X-CC2-Request':'1','Origin':'http://untrusted.example'})[0]==403
            assert call('/api/network/select','POST',{'X-CC2-Request':'1','Host':'untrusted.example'})[0]==403
            assert call('/api/network',headers={'Origin':'http://untrusted.example'})[0]==403
            assert call('/api/network',headers={'Host':'untrusted.example'})[0]==403
            assert data['snapshot']['capabilities']=={'network_changes':False,'automatic_selection':False}
            assert not errors,errors
        finally:
            process.terminate();process.wait(timeout=5)
finally:
    done.set();worker.join(2);server.close();peer.unlink()
print('PASS: real CC2 GET-only API, slow-WPA isolation, no idle polling, concurrent-tab coalescing, method/origin guards')
