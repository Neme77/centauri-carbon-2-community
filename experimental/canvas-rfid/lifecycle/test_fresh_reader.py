"""Actual read-only observer against bounded loopback fixtures, host/ARM/ASan.

No vendor executable, device, process stop or firmware transfer is used.
"""
from pathlib import Path
import copy
import json
import signal
import socketserver
import subprocess
import tempfile
import threading
import time

HERE = Path(__file__).resolve().parent
PRINTER = {'connected': True, 'last_message_age': 2,
    'machine': {'status': 1}, 'extruder': {'temperature': 24, 'target': 0},
    'heater_bed': {'temperature': 22, 'target': 0},
    'print': {'enabled': False, 'native': {'state': 'standby'}},
    'recovery': {'available': False, 'reboot_pending': False},
    'bed_drying': {'active': False}}
UDS = {'connected': True, 'fresh': True, 'print_state': 'standby',
    'values': {'nozzle_target': 0, 'bed_target': 0, 'live_velocity': 0,
               'nozzle_temperature': 24, 'bed_temperature': 22}}
CASES = ['standby', 'cancelled', 'complete', 'stale', 'negative-age',
    'disconnected', 'printing', 'native-printing', 'state-mismatch',
    'enabled-standby', 'nozzle-target', 'bed-target', 'hot-nozzle', 'hot-bed',
    'native-target', 'native-hot', 'motion', 'native-stale', 'recovery',
    'reboot', 'drying', 'missing', 'duplicate-json', 'http-error',
    'duplicate-length', 'chunked', 'truncated', 'oversized', 'cancel']

class Server(socketserver.ThreadingTCPServer):
    allow_reuse_address = True
    daemon_threads = True

class Handler(socketserver.BaseRequestHandler):
    def handle(self):
        self.request.settimeout(3)
        request = b''
        while b'\r\n\r\n' not in request:
            part = self.request.recv(512)
            if not part or len(request) > 2048:
                return
            request += part
        endpoint = request.split(b' ', 2)[1]
        assert endpoint in [b'/api/printer', b'/api/uds'], request
        self.server.seen.append(endpoint)
        case = self.server.mode
        if case == 'cancel':
            self.server.observed.set()
            time.sleep(.3)
            return
        p, u = copy.deepcopy(PRINTER), copy.deepcopy(UDS)
        if case in ['cancelled', 'complete']:
            p['print'] = {'enabled': True, 'native': {'state': case}}
            u['print_state'] = case
        if case == 'stale': p['last_message_age'] = 6
        if case == 'negative-age': p['last_message_age'] = -1
        if case == 'disconnected': p['connected'] = False
        if case == 'printing': p['machine']['status'] = 2
        if case == 'native-printing':
            p['print']['native']['state'] = u['print_state'] = 'printing'
        if case == 'state-mismatch': u['print_state'] = 'complete'
        if case == 'enabled-standby': p['print']['enabled'] = True
        if case == 'nozzle-target': p['extruder']['target'] = 1
        if case == 'bed-target': p['heater_bed']['target'] = 1
        if case == 'hot-nozzle': p['extruder']['temperature'] = 51
        if case == 'hot-bed': p['heater_bed']['temperature'] = 51
        if case == 'native-target': u['values']['bed_target'] = 1
        if case == 'native-hot': u['values']['nozzle_temperature'] = 51
        if case == 'motion': u['values']['live_velocity'] = .01
        if case == 'native-stale': u['fresh'] = False
        if case == 'recovery': p['recovery']['available'] = True
        if case == 'reboot': p['recovery']['reboot_pending'] = True
        if case == 'drying': p['bed_drying']['active'] = True
        if case == 'missing': del p['recovery']
        body = json.dumps(p if endpoint == b'/api/printer' else u).encode()
        if case == 'duplicate-json': body = b'{"connected":true,' + body[1:]
        if case == 'oversized': body = b'x' * 32768
        status = b'503' if case == 'http-error' else b'200'
        header = b'HTTP/1.0 ' + status + b' Test\r\nContent-Length: ' + str(len(body)).encode() + b'\r\n'
        if case == 'duplicate-length': header += b'Content-Length: ' + str(len(body)).encode() + b'\r\n'
        if case == 'chunked': header += b'Transfer-Encoding: chunked\r\n'
        response = header + b'Connection: close\r\n\r\n' + (body[:-1] if case == 'truncated' else body)
        try:
            for part in [response[:7], response[7:31], response[31:]]:
                self.request.sendall(part)
        except (BrokenPipeError, ConnectionResetError):
            pass

def run():
    count = 0
    with tempfile.TemporaryDirectory(prefix='cc2-fresh-reader-') as temporary:
        root = Path(temporary)
        with Server(('127.0.0.1', 0), Handler) as server:
            thread = threading.Thread(target=server.serve_forever, daemon=True)
            thread.start()
            try:
                for arch, compiler, flags, runner in [
                    ('host', 'gcc', [], []),
                    ('arm', 'arm-linux-gnueabihf-gcc', ['-static', '-D_TIME_BITS=32', '-D_FILE_OFFSET_BITS=32'], ['qemu-arm']),
                    ('sanitizers', 'gcc', ['-fsanitize=address,undefined', '-fno-sanitize-recover=all', '-fno-pie', '-no-pie'], [])]:
                    binary = root / arch
                    subprocess.run([compiler, '-O2', '-std=c11', '-Wall', '-Wextra', '-Wpedantic', '-Werror',
                        '-D_POSIX_C_SOURCE=200809L', f'-DVC_PORT={server.server_address[1]}',
                        *flags, HERE / 'fresh_reader.c', '-lm', '-o', binary], check=True, timeout=120)
                    for case in CASES:
                        server.mode = case
                        server.seen = []
                        server.observed = threading.Event()
                        process = subprocess.Popen([*runner, binary, '--check'], stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
                        if case == 'cancel':
                            assert server.observed.wait(3), arch
                            process.send_signal(signal.SIGTERM)
                        stdout, stderr = process.communicate(timeout=10)
                        expected = case in ['standby', 'complete', 'cancelled']
                        assert (process.returncode == 0) == expected, (arch, case, stdout, stderr)
                        assert server.seen and set(server.seen) <= {b'/api/printer', b'/api/uds'}
                        if expected: assert server.seen == [b'/api/printer', b'/api/uds']
                        count += 1
            finally:
                server.shutdown()
                thread.join(timeout=5)
    print(f'PASS: {count} actual read-only freshness host/ARM/sanitizer cases; terminal cancellation, zero targets, motion, recovery and bounded HTTP/cancellation')
    return count

if __name__ == '__main__':
    run()
