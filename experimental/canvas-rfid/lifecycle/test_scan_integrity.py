"""Actual immutable scan coordinator; inert processes and loopback only.

Count native hash passes and mutate evidence across the live-state boundary.
No printer, vendor executable, physical UART or firmware transfer is used.
"""
from pathlib import Path
import hashlib
import json
import os
import shutil
import signal
import struct
import subprocess
import tempfile
import threading
import time

from test_post_boot import Server
from test_stage_worker import fixture, put, spawn, seed_journal, MODULES

HERE = Path(__file__).resolve().parent
sha = lambda data: hashlib.sha256(data).hexdigest()
CASES = ('ready', 'repeat', 'wrong-phase', 'profile-changed', 'profile-native-alias',
         'backup-bad', 'missing-module', 'duplicate-native', 'ready-busy', 'busy',
         'query-refused', 'query-malformed', 'native-replaced', 'app-restarted',
         'module-changed', 'nonce-changed', 'journal-changed', 'cancelled')


class Handler(__import__('socketserver').BaseRequestHandler):
    def handle(self):
        request = b''
        while b'\r\n\r\n' not in request:
            part = self.request.recv(512)
            if not part or len(request) > 2048:
                return
            request += part
        assert request.startswith(b'GET /api/rfid/lifecycle/scan-readiness HTTP/1.0\r\n'), request
        server = self.server
        server.calls += 1
        server.change()
        server.entered.set()
        if server.case == 'cancelled':
            time.sleep(.5)
        body = b'{"ready":false}' if server.case == 'query-refused' else b'not JSON' if server.case == 'query-malformed' else b'{"ready":true}'
        try:
            self.request.sendall(b'HTTP/1.0 200 OK\r\nContent-Length: ' + str(len(body)).encode() + b'\r\nConnection: close\r\n\r\n' + body)
        except (BrokenPipeError, ConnectionResetError):
            pass


def run(architectures):
    assert os.geteuid() == 0, 'Run these private-file fixtures as root in WSL'
    stock = Path(os.environ['CC2_TEST_STOCK_CANVAS']).read_bytes()
    assert sha(stock) == 'f1267ee3219dcad73efffa8dc38e30ff0f17c02c0a1674cbead48a99bdff3228'
    flags = ['-O2', '-std=c11', '-Wall', '-Wextra', '-Wpedantic', '-Werror', '-D_POSIX_C_SOURCE=200809L']
    count = 0
    with tempfile.TemporaryDirectory(prefix='cc2-scan-integrity-') as temporary:
        root = Path(temporary)
        dummy_source = root / 'dummy.c'
        dummy = root / 'dummy'
        dummy_source.write_text('''#include <fcntl.h>
#include <stdio.h>
#include <sys/mman.h>
#include <unistd.h>
int main(int n,char **v){
 for(int i=1;i<n;i++){
  int f=open(v[i],O_RDONLY);if(f<0)return 2;
  for(int j=0;j<3;j++)if(mmap(0,4096,j==0?PROT_READ:j==1?PROT_NONE:PROT_READ|PROT_WRITE,MAP_PRIVATE,f,0)==MAP_FAILED)return 3;
  close(f);
 }
 puts("READY");fflush(stdout);for(;;)pause();
}
''')
        subprocess.run(['gcc', *flags, str(dummy_source), '-o', str(dummy)], check=True, timeout=60)
        dummy_bytes = dummy.read_bytes()
        driver = root / 'driver.c'
        driver.write_text('''#define _GNU_SOURCE
#include <sys/stat.h>
#include "sha256.h"
static unsigned native_hashes,cavity_hashes;
static int counted_hash(int fd,char out[65]){
 struct stat a,b;const char *path=getenv("TEST_NATIVE_FILE");
 if(path&&!fstat(fd,&a)&&!stat(path,&b)&&a.st_dev==b.st_dev&&a.st_ino==b.st_ino)native_hashes++;
 path=getenv("TEST_CAVITY_FILE");
 if(path&&!fstat(fd,&a)&&!stat(path,&b)&&a.st_dev==b.st_dev&&a.st_ino==b.st_ino)cavity_hashes++;
 return sha256_fd(fd,out);
}
#define sha256_fd counted_hash
#define VC_ENTRY actual_coordinator
#include "coordinator.c"
int main(int argc,char **argv){int result=actual_coordinator(argc,argv);printf("NATIVE_HASH_PASSES=%u\\nCAVITY_HASH_PASSES=%u\\n",native_hashes,cavity_hashes);return result;}
''')
        with Server(('127.0.0.1', 0), Handler) as server:
            thread = threading.Thread(target=server.serve_forever, daemon=True)
            thread.start()
            try:
                for arch, compiler, extra, runner in architectures:
                    folder = root / arch

                    def make_fixture():
                        if folder.exists():
                            shutil.rmtree(folder)
                        component, tree, native, app, _, _, profile, fresh = fixture(folder, dummy_bytes, stock)
                        cavity = tree / 'opt/inst/cc2-printer-extensions13/libcc2-cavity-floor-r1.so'
                        put(cavity, (tree / MODULES[1][1:]).read_bytes())
                        put(component / 'original-stock-44.bin', stock)
                        seed_journal(component, profile, 5)
                        return component, tree, native, app, profile, fresh, cavity

                    component, tree, native, app, profile, fresh, cavity = make_fixture()
                    defines = {'SC_BASE': str(component), 'SC_PREFIX': str(tree), 'SC_PROFILE_SHA': sha(profile),
                               'SC_APP_SHA': sha(dummy_bytes), 'SC_NATIVE_SHA': sha(dummy_bytes),
                               'SC_FRESH_SHA': sha(fresh), 'SC_RUN_SHA': sha((tree / 'opt/bin/run_printer.sh').read_bytes()),
                               'SC_READY': str(folder / 'ready'), 'CC2_MAINTENANCE_LOCK': str(folder / 'install.lock'),
                               'SC_STRICT_SHA': sha((component / 'libcc2-rfid.so').read_bytes())}
                    for key, path in zip(('SC_REACTOR_SHA', 'SC_CAVITY_SHA', 'SC_NETWORK_SHA', 'SC_OLD_RFID_SHA'), MODULES):
                        defines[key] = sha((tree / path[1:]).read_bytes())

                    def compile_driver(binary, selected):
                        subprocess.run([compiler, *flags, *extra, '-DSC_IMMUTABLE_BOOT=1', '-DSC_STOCK_BOOTSTRAP=1', '-DSC_NATIVE_RESTORE_GUARD=1',
                                        '-DVC_PORT=' + str(server.server_address[1]),
                                        *['-D' + key + '=' + json.dumps(value) for key, value in selected.items()],
                                        '-I' + str(HERE), '-I' + str(HERE.parent / 'host'), str(driver), '-lm', '-o', str(binary)],
                                       check=True, timeout=120)

                    binary = root / (arch + '-coordinator')
                    compile_driver(binary, defines)
                    for case in CASES:
                        component, tree, native, app, profile, _, cavity = make_fixture()
                        processes = []
                        server.case = case
                        server.calls = 0
                        server.entered = threading.Event()
                        server.change = lambda: None
                        modules = [tree / MODULES[0][1:], cavity, tree / MODULES[2][1:], component / 'libcc2-rfid.so']
                        if case == 'missing-module':
                            modules.remove(cavity)
                        chosen_binary = binary
                        if case == 'profile-native-alias':
                            alias = tree / 'opt/bin/alias-native'
                            put(alias, dummy_bytes)
                            profile = profile.replace(str(native).encode(), str(alias).encode())
                            put(component / 'recovery-profile', profile)
                            seed_journal(component, profile, 5)
                            chosen_binary = root / (arch + '-alias-coordinator')
                            compile_driver(chosen_binary, {**defines, 'SC_PROFILE_SHA': sha(profile)})
                        app_process = spawn(app, [])
                        processes.append(app_process)
                        native_process = spawn(native, modules)
                        processes.append(native_process)
                        put(folder / 'ready', struct.pack('<8sQQQII', b'CC2RF1\0\0', 123, 0, 0, native_process.pid, 1))
                        if case == 'wrong-phase':
                            seed_journal(component, profile, 4)
                        elif case == 'profile-changed':
                            put(component / 'recovery-profile', profile + b'changed')
                        elif case == 'backup-bad':
                            put(component / 'original-stock-44.bin', b'bad')
                        elif case == 'duplicate-native':
                            processes.append(spawn(native, modules))
                        elif case == 'ready-busy':
                            put(folder / 'ready', struct.pack('<8sQQQII', b'CC2RF1\0\0', 123, 1, 2**40, native_process.pid, 1))
                        elif case == 'busy':
                            (folder / 'install.lock').mkdir(mode=0o700)

                        def change():
                            if case == 'native-replaced':
                                native.rename(native.with_name('previous-native'))
                                put(native, dummy_bytes, 0o700)
                            elif case == 'app-restarted':
                                app_process.terminate()
                                app_process.wait(timeout=5)
                                processes.append(spawn(app, []))
                            elif case == 'module-changed':
                                put(cavity, b'changed module')
                            elif case == 'nonce-changed':
                                put(folder / 'ready', struct.pack('<8sQQQII', b'CC2RF1\0\0', 124, 0, 0, native_process.pid, 1))
                            elif case == 'journal-changed':
                                journal = bytearray((component / 'lifecycle').read_bytes()[:128])
                                struct.pack_into('<Q', journal, 24, 2)
                                put(component / 'lifecycle', journal + sha(journal).encode())
                        server.change = change
                        before = (component / 'lifecycle').read_bytes()
                        env = {**os.environ, 'TEST_NATIVE_FILE': str(native), 'TEST_CAVITY_FILE': str(cavity)}
                        try:
                            child = subprocess.Popen([*runner, str(chosen_binary), '--check-active'], env=env,
                                                     stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
                            if case == 'cancelled':
                                assert server.entered.wait(10), (arch, case, 'readiness not reached')
                                child.send_signal(signal.SIGTERM)
                            stdout, stderr = child.communicate(timeout=20)
                            expected = case in ('ready', 'repeat')
                            assert (child.returncode == 0) == expected, (arch, case, stdout, stderr)
                            if expected:
                                assert 'NATIVE_HASH_PASSES=2\n' in stdout, (arch, case, stdout)
                                assert 'CAVITY_HASH_PASSES=2\n' in stdout, (arch, case, stdout)
                            if case == 'repeat':
                                result = subprocess.run([*runner, str(binary), '--check-active'], env=env,
                                                        capture_output=True, text=True, timeout=20)
                                assert result.returncode == 0 and 'NATIVE_HASH_PASSES=2\n' in result.stdout
                                put(cavity, b'changed between scans')
                                result = subprocess.run([*runner, str(binary), '--check-active'], env=env,
                                                        capture_output=True, text=True, timeout=20)
                                assert result.returncode != 0, (arch, case, 'cross-scan hash reused')
                            if case != 'journal-changed':
                                assert (component / 'lifecycle').read_bytes() == before, (arch, case, 'journal changed')
                            else:
                                assert (component / 'lifecycle').read_bytes()[24] == 2
                            assert (folder / 'install.lock').exists() == (case == 'busy'), (arch, case, 'lock retained')
                            assert native_process.poll() is None, (arch, case, 'native process stopped')
                            count += 1
                            print(f'PASS {arch} immutable scan integrity {case}', flush=True)
                        finally:
                            for process in processes:
                                if process.poll() is None:
                                    process.terminate()
                                process.wait(timeout=5)
            finally:
                server.shutdown()
                thread.join(timeout=5)
    print(f'PASS: {count} real immutable scan checks; two native hashes per scan, live-state and identity refusals preserved')
    return count
