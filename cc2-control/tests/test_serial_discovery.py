"""Exercise first-run HTTP serial discovery without a printer or broker."""
import pathlib, subprocess, tempfile, threading, socket, time
ROOT = pathlib.Path(__file__).resolve().parents[1]
with tempfile.TemporaryDirectory() as directory:
    directory = pathlib.Path(directory)
    server = socket.socket()
    server.bind(('127.0.0.1', 0)); server.listen()
    port = server.getsockname()[1]
    harness = directory / 'probe.c'
    harness.write_text('''#include <signal.h>
#include <stdio.h>
#include <string.h>
#include "mqtt.c"
int main(int argc,char **argv){
 signal(SIGPIPE,SIG_IGN); mqtt_client c; mqtt_init(&c); strcpy(c.password,"a.b_-9");
 snprintf(c.config_path,sizeof(c.config_path),"%s",argv[1]);
 c.connected=1; c.fd=socket(AF_INET,SOCK_STREAM,0);
 if(argc>2)strcpy(c.serial,"EXISTING");
 mqtt_tick(&c); unsigned int attempts=c.serial_discovery_attempts;
 mqtt_tick(&c);
 printf("%s %u %u\\n",c.serial,attempts,c.serial_discovery_attempts);
 close(c.fd); return 0;
}''')
    binary=directory/'probe'
    subprocess.run(['gcc','-std=c11','-D_POSIX_C_SOURCE=200809L',f'-DCC2_DISCOVERY_PORT={port}', '-I'+str(ROOT/'src'),str(harness),'-o',str(binary)],check=True)
    def run_case(body, status=200, stall=False, expected=''):
        requests=[]
        def serve():
            peer,_=server.accept()
            with peer:
                requests.append(peer.recv(2048))
                if stall: time.sleep(0.8)
                else:
                    response=f'HTTP/1.0 {status} Test\r\nContent-Length: {len(body)}\r\n\r\n'+body
                    # Fragment the response to exercise accumulation.
                    peer.sendall(response[:19].encode()); peer.sendall(response[19:].encode())
        thread=threading.Thread(target=serve);thread.start()
        config=directory/'config'; config.write_text('mqtt_serial=\n')
        start=time.monotonic()
        result=subprocess.run([str(binary),str(config)],capture_output=True,text=True,timeout=2,check=True)
        elapsed=time.monotonic()-start
        thread.join()
        assert result.stdout.strip()==f'{expected} 1 1'.strip(),result.stdout
        assert b'X-Token=%61%2E%62%5F%2D%39' in requests[0]
        if expected: assert f'mqtt_serial={expected}\n' in config.read_text()
        else: assert config.read_text()=='mqtt_serial=\n'
        if stall: assert elapsed<0.7,elapsed
    run_case('{"system_info":{"sn":"TEST_CC2_SERIAL"}}',expected='TEST_CC2_SERIAL')
    run_case('{"system_info":{"sn":"WRONG"}}',status=401)
    run_case('{"system_info":{"sn":""}}')
    run_case('{"system_info":{"sn":"bad/topic"}}')
    run_case('{"system_info":{"sn":"'+('A'*80)+'"}}')
    run_case('{"other":{"sn":"WRONG"}}')
    run_case('',stall=True)
    result=subprocess.run([str(binary),str(directory/'config'),'existing'],capture_output=True,text=True,check=True)
    assert result.stdout.strip()=='EXISTING 0 0'
    server.close()
print('HTTP serial discovery: success, persistence, authentication failure, invalid/missing serial, fragmentation, timeout, retry throttling and saved-serial bypass PASS')
