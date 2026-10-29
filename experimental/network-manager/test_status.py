"""Real executable/local WPA socket tests. Never require a printer or passwords."""
import json
import os
from pathlib import Path
import socket
import subprocess
import tempfile
import threading
import time

ROOT = Path(__file__).resolve().parent
FLAGS = ['-std=c11', '-O1', '-g', '-Wall', '-Wextra', '-Wpedantic', '-Werror', '-UNDEBUG']


def run(args, **kwargs):
    return subprocess.run([str(x) for x in args], check=True, timeout=20, **kwargs)


def main():
    out = ROOT / 'output'
    out.mkdir(exist_ok=True)
    run(['gcc', *FLAGS, '-fsanitize=address,undefined', '-fno-omit-frame-pointer',
         '-fno-pie', '-no-pie', ROOT/'test_status_core.c', '-o', out/'core-tests'])
    run([out/'core-tests'])
    with tempfile.TemporaryDirectory(prefix='cc2-net-') as tmp:
        endpoint = Path(tmp)/'wlan0'
        binary = out/'status-fixture'
        run(['gcc', *FLAGS, '-fsanitize=address,undefined', '-fno-omit-frame-pointer',
             '-fno-pie', '-no-pie', f'-DCC2_NETWORK_STATUS_TEST="{endpoint}"', ROOT/'status.c', '-o', binary])
        invalid = subprocess.run([binary, '--change'], capture_output=True, timeout=5)
        assert invalid.returncode == 2 and invalid.stdout == b''
        cases = [
            (b'wpa_state=COMPLETED\nssid=My "Wi-Fi"\\test\nfreq=5220\nkey_mgmt=WPA2-PSK\n',
             b'RSSI=-51\nLINKSPEED=433\n', True, -51),
            (b'wpa_state=DISCONNECTED\n', None, True, None),
            (b'wpa_state=COMPLETED\nwpa_state=DISCONNECTED\n', None, False, None),
            (b'wpa_state=COMPLETED', None, False, None),
            (b'wpa_state=COMPLETED\n\x00ssid=hidden\n', None, False, None),
            (b'wpa_state=COMPLETED\nssid=ok\n', b'RSSI=0\n', True, None),
            (b'wpa_state=COMPLETED\nssid=ok\n', b'RSSI=-51;reboot\n', True, None),
            (b'wpa_state=COMPLETED\nssid=ok\n', b'FAIL\n', True, None),
            (b'wpa_state=COMPLETED\nssid='+b'x'*5000+b'\n', None, False, None),
            (None, None, False, None),
            ([None, b'wpa_state=COMPLETED\nssid=Recovered reading\nfreq=5220\n'], b'RSSI=-45\n', True, -45),
        ]
        for status, signal, available, rssi in cases:
            requests=[]; failures=[]; stop=threading.Event()
            server=socket.socket(socket.AF_UNIX, socket.SOCK_DGRAM)
            server.bind(str(endpoint)); server.settimeout(.1)

            def serve():
                while not stop.is_set():
                    try:
                        command, address=server.recvfrom(256)
                    except socket.timeout:
                        continue
                    except OSError:
                        return
                    requests.append(command)
                    if command not in (b'STATUS', b'SIGNAL_POLL'):
                        failures.append(command); return
                    reply=status if command==b'STATUS' else signal
                    if isinstance(reply,list):
                        reply=reply[min(requests.count(b'STATUS')-1,len(reply)-1)]
                    if reply is not None:
                        try:
                            server.sendto(reply, address)
                        except OSError as e:
                            failures.append(str(e))

            thread=threading.Thread(target=serve, daemon=True);thread.start()
            started=time.monotonic()
            try:
                result=run([binary], capture_output=True, text=True)
                assert time.monotonic()-started < 2, 'bounded read must not hang HTTP'
                data=json.loads(result.stdout)
                assert data['wireless']['control_available'] is available, data
                assert data['wireless']['signal_dbm'] == rssi, data
                read=data['wireless']['status_read']
                if isinstance(status,list):
                    assert read['attempts']==2 and read['first_error']=='timeout' and read['error']=='none',read
                elif status is None:
                    assert read['attempts']==2 and read['first_error']=='timeout' and read['error']=='timeout',read
                else:
                    assert read['attempts']==1,read
                if b'ssid=My ' in (status or b''):
                    assert data['wireless']['ssid']=='My "Wi-Fi"\\test'
                assert data['read_only'] and not data['capabilities']['network_changes']
                assert data['route']['internet']=='untested'
                assert set(requests) <= {b'STATUS',b'SIGNAL_POLL'} and requests[0]==b'STATUS'
                assert not failures, failures
            finally:
                stop.set();thread.join(2);server.close();endpoint.unlink()
        missing=run([binary], capture_output=True, text=True)
        missing_data=json.loads(missing.stdout)['wireless']
        assert missing_data['control_available'] is False
        assert missing_data['status_read']['error']=='connect' and missing_data['status_read']['errno']==2
    print('PASS: 11 real WPA datagram fixtures, bounded retry with retained first failure, missing peer, CLI refusal, ASAN/UBSAN, no mutation commands')


if __name__=='__main__':
    main()
