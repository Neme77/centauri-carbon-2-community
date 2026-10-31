"""Run production WPA rollback commands against a real local datagram socket."""
from pathlib import Path
import os,socket,subprocess,sys,threading

binary=Path(sys.argv[1]).resolve()
address='/tmp/cc2-connections-wpa-test.sock'
assert not os.path.lexists(address)
commands=[]
sock=socket.socket(socket.AF_UNIX,socket.SOCK_DGRAM)
sock.bind(address)
sock.settimeout(0.1)
stop=False
def server():
    while not stop:
        try: command,peer=sock.recvfrom(4096)
        except socket.timeout: continue
        text=command.decode();commands.append(text)
        if text=='LIST_NETWORKS': reply='network id / ssid / bssid / flags\n0\tOld fixture\tany\t[DISABLED]\n1\tNew fixture\tany\t[CURRENT]\n2\tOther fixture\tany\t[DISABLED]\n'
        elif text.startswith('GET_NETWORK '): reply={'0':'1\n','1':'12\n','2':'0\n'}[text.split()[1]]
        elif text=='STATUS':reply='id=1\nwpa_state=COMPLETED\nssid=New fixture\n'
        else:reply='OK\n'
        sock.sendto(reply.encode(),peer)
thread=threading.Thread(target=server);thread.start()
try:
    subprocess.run([binary,'--wifi-restore'],check=True,timeout=15)
    assert 'REMOVE_NETWORK 1' in commands
    assert 'REMOVE_NETWORK 8' not in commands
    assert 'SELECT_NETWORK 0' in commands
    assert 'SET_NETWORK 0 priority 1' in commands
    assert 'DISABLE_NETWORK 2' in commands
    assert commands[-1]=='SAVE_CONFIG'
    assert not any('psk' in command for command in commands)
    print('PASS: remapped native profile IDs, selected/disabled flags restored, no password read/copied')
finally:
    stop=True;thread.join();sock.close();os.unlink(address)
